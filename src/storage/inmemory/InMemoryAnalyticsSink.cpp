#include "url_shortener/storage/inmemory/InMemoryAnalyticsSink.hpp"

bool InMemoryAnalyticsSink::Emit(const LinkAccessEvent& event, AnalyticsError* error)
{
    std::lock_guard lock(mutex_);
    events_.push_back(event);
    if (error != nullptr) {
        *error = AnalyticsError::none;
    }
    return true;
}

bool InMemoryAnalyticsSink::Flush(AnalyticsError* error)
{
    if (error != nullptr) {
        *error = AnalyticsError::none;
    }
    return true;
}

std::vector<LinkAccessEvent> InMemoryAnalyticsSink::Snapshot() const
{
    std::lock_guard lock(mutex_);
    return events_;
}
