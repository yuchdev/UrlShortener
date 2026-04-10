#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"

#include <mutex>
InMemoryCacheStore::InMemoryCacheStore(const IClock& clock)
    : clock_(clock)
{
}

std::optional<CacheValue> InMemoryCacheStore::Get(const std::string& key, CacheError* error)
{
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        if (error != nullptr) {
            *error = CacheError::none;
        }
        return std::nullopt;
    }

    if (it->second.expires_at.has_value() && it->second.expires_at.value() <= clock_.now()) {
        entries_.erase(it);
        if (error != nullptr) {
            *error = CacheError::none;
        }
        return std::nullopt;
    }

    if (error != nullptr) {
        *error = CacheError::none;
    }
    return it->second.value;
}

bool InMemoryCacheStore::Set(const std::string& key,
                             const CacheValue& value,
                             std::optional<std::chrono::seconds> ttl,
                             CacheError* error)
{
    std::unique_lock lock(mutex_);
    Entry entry{value, std::nullopt};
    if (ttl.has_value()) {
        if (ttl.value().count() <= 0) {
            entries_.erase(key);
            if (error != nullptr) {
                *error = CacheError::none;
            }
            return true;
        }
        entry.expires_at = clock_.now() + ttl.value();
    }
    entries_[key] = entry;
    if (error != nullptr) {
        *error = CacheError::none;
    }
    return true;
}

bool InMemoryCacheStore::Delete(const std::string& key, CacheError* error)
{
    std::unique_lock lock(mutex_);
    entries_.erase(key);
    if (error != nullptr) {
        *error = CacheError::none;
    }
    return true;
}

bool InMemoryCacheStore::ClearByPrefix(const std::string& prefix, CacheError* error)
{
    std::unique_lock lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.rfind(prefix, 0) == 0) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    if (error != nullptr) {
        *error = CacheError::none;
    }
    return true;
}
