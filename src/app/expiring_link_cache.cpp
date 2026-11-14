#include <utility>

#include <url_shortener/app/expiring_link_cache.hpp>

namespace url_shortener
{
namespace app
{

ExpiringLinkCache::ExpiringLinkCache(const IClock& clock,
                                     std::chrono::seconds ttl,
                                     std::size_t max_entries)
    : m_clock_(clock)
    , m_ttl_(ttl)
    , m_max_entries_(max_entries)
{
}

bool ExpiringLinkCache::is_expired(
    const entry& e, std::chrono::system_clock::time_point now) const
{
    if (m_ttl_ == std::chrono::seconds::zero()) {
        return false;
    }
    return now - e.stored_at >= m_ttl_;
}

std::optional<Link> ExpiringLinkCache::get(const std::string& slug)
{
    const auto now = m_clock_.now();
    std::lock_guard<std::mutex> lock(m_mutex_);

    const auto it = m_entries_.find(slug);
    if (it == m_entries_.end()) {
        return std::nullopt;
    }
    if (is_expired(it->second, now)) {
        m_entries_.erase(it);
        return std::nullopt;
    }
    return it->second.link;
}

void ExpiringLinkCache::set(const std::string& slug, const Link& link)
{
    const auto now = m_clock_.now();
    std::lock_guard<std::mutex> lock(m_mutex_);

    const auto existing = m_entries_.find(slug);
    if (existing != m_entries_.end()) {
        existing->second.link = link;
        existing->second.stored_at = now;
        return;
    }

    // Inserting a new key: enforce the size bound. Drop expired
    // entries first; if still full, drop one arbitrary live entry.
    if (m_max_entries_ > 0 && m_entries_.size() >= m_max_entries_) {
        for (auto it = m_entries_.begin(); it != m_entries_.end();) {
            if (is_expired(it->second, now)) {
                it = m_entries_.erase(it);
            }
            else {
                ++it;
            }
        }
        if (m_entries_.size() >= m_max_entries_) {
            m_entries_.erase(m_entries_.begin());
        }
    }

    entry e;
    e.link = link;
    e.stored_at = now;
    m_entries_.emplace(slug, std::move(e));
}

void ExpiringLinkCache::erase(const std::string& slug)
{
    std::lock_guard<std::mutex> lock(m_mutex_);
    m_entries_.erase(slug);
}

bool ExpiringLinkCache::patch(const std::string& slug,
                              const std::function<void(Link&)>& mutator)
{
    const auto now = m_clock_.now();
    std::lock_guard<std::mutex> lock(m_mutex_);

    const auto it = m_entries_.find(slug);
    if (it == m_entries_.end()) {
        return false;
    }
    if (is_expired(it->second, now)) {
        m_entries_.erase(it);
        return false;
    }
    mutator(it->second.link);
    return true;
}

void ExpiringLinkCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex_);
    m_entries_.clear();
}

std::size_t ExpiringLinkCache::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex_);
    return m_entries_.size();
}

}  // namespace app
}  // namespace url_shortener
