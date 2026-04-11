#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"

#include <algorithm>
#include <mutex>

bool InMemoryMetadataRepository::CreateLink(const CreateLinkRequest& request,
                                            const std::string& id,
                                            const std::chrono::system_clock::time_point& now,
                                            RepoError* error)
{
    if (!request.isValid()) {
        if (error != nullptr) {
            *error = RepoError::invalid_argument;
        }
        return false;
    }

    std::unique_lock lock(mutex_);
    if (by_code_.find(request.short_code) != by_code_.end()) {
        if (error != nullptr) {
            *error = RepoError::already_exists;
        }
        return false;
    }

    LinkRecord record;
    record.id = id;
    record.short_code = request.short_code;
    record.target_url = request.target_url;
    record.created_at = now;
    record.updated_at = now;
    record.expires_at = request.expires_at;
    record.owner = request.owner;
    record.attributes = request.attributes;
    record.is_active = request.is_active;
    by_code_[record.short_code] = record;
    if (error != nullptr) {
        *error = RepoError::none;
    }
    return true;
}

std::optional<LinkRecord> InMemoryMetadataRepository::GetByShortCode(const std::string& short_code,
                                                                     RepoError* error) const
{
    std::shared_lock lock(mutex_);
    if (const auto it = by_code_.find(short_code); it != by_code_.end()) {
        if (error != nullptr) {
            *error = RepoError::none;
        }
        return it->second;
    }
    if (error != nullptr) {
        *error = RepoError::not_found;
    }
    return std::nullopt;
}

bool InMemoryMetadataRepository::UpdateLink(const LinkRecord& record, RepoError* error)
{
    std::unique_lock lock(mutex_);
    const auto it = by_code_.find(record.short_code);
    if (it == by_code_.end()) {
        if (error != nullptr) {
            *error = RepoError::not_found;
        }
        return false;
    }
    it->second = record;
    if (error != nullptr) {
        *error = RepoError::none;
    }
    return true;
}

bool InMemoryMetadataRepository::DeleteLink(const std::string& short_code, RepoError* error)
{
    std::unique_lock lock(mutex_);
    by_code_.erase(short_code);
    if (error != nullptr) {
        *error = RepoError::none;
    }
    return true;
}

std::vector<LinkRecord> InMemoryMetadataRepository::ListLinks(const ListLinksQuery& query, RepoError* error) const
{
    std::shared_lock lock(mutex_);
    std::vector<LinkRecord> records;
    records.reserve(by_code_.size());
    for (const auto& [_, record] : by_code_) {
        if (!query.include_deleted && record.deleted_at.has_value()) {
            continue;
        }
        if (!query.include_inactive && !record.is_active) {
            continue;
        }
        if (query.owner.has_value() && record.owner != query.owner) {
            continue;
        }
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](const LinkRecord& lhs, const LinkRecord& rhs) {
        return lhs.created_at < rhs.created_at;
    });

    if (query.offset >= records.size()) {
        if (error != nullptr) {
            *error = RepoError::none;
        }
        return {};
    }

    const std::size_t remaining = records.size() - query.offset;
    const std::size_t limit = std::min(query.limit, remaining);
    const std::size_t end = query.offset + limit;
    using DifferenceType = std::vector<LinkRecord>::difference_type;
    std::vector<LinkRecord> paged(records.begin() + static_cast<DifferenceType>(query.offset),
                                  records.begin() + static_cast<DifferenceType>(end));
    if (error != nullptr) {
        *error = RepoError::none;
    }
    return paged;
}

bool InMemoryMetadataRepository::Exists(const std::string& short_code, RepoError* error) const
{
    const bool exists = GetByShortCode(short_code, error).has_value();
    if (error != nullptr) {
        *error = RepoError::none;
    }
    return exists;
}
