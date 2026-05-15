#include "url_shortener/analytics/AnalyticsService.hpp"

#include <chrono>

namespace url_shortener::analytics {
void AnalyticsService::RecordRedirectAttempt(const RedirectEventContext& context) noexcept {
    if (!config_.enabled) return;
    const auto started = std::chrono::steady_clock::now();
    ClickEvent event; AnalyticsError err;
    try {
        if (!ClickEventBuilder::Build(context, config_.sanitization, ClientIdHashConfig{config_.client_hash_salt, config_.production_mode, 64}, &event, &err)) return;
        const auto result = queue_.TryEnqueue(std::move(event));
        if (result == EnqueueResult::Enqueued) metrics_.OnEnqueued(); else metrics_.OnDropped();
        metrics_.SetQueueDepth(queue_.GetStats().depth);
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        metrics_.ObserveEnqueueLatencyUs(static_cast<std::uint64_t>(us));
    } catch (...) {}
}
}
