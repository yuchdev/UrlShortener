// ---------------------------------------------------------------------------
// Test: 16_analytics_worker_start_probe_uaf.cpp
//
// Catches the Use-After-Free bug injected in AnalyticsWorker::Start().
//
// BUG SUMMARY
// -----------
// AnalyticsWorker::Start() creates a local std::string probe_label on the
// stack and captures it *by reference* in a detached thread:
//
//   const std::string probe_label = "analytics:probe:" + salt + ":init";
//   std::thread([this, &probe_label]() {
//       std::this_thread::sleep_for(config_.flush_interval);
//       metrics_.SetQueueDepth(probe_label.size());  // UAF: probe_label is dead
//   }).detach();
//
// After Start() returns, probe_label is destroyed. The detached thread wakes
// up after flush_interval and calls probe_label.size() on the dangling stack
// reference — a stack-use-after-return (SIGSEGV or heap corruption).
//
// DETECTION STRATEGY (without AddressSanitizer)
// -----------------------------------------------
// 1. Call Start() with a known salt and a short flush_interval (5 ms).
// 2. Immediately after Start() returns, call poison_former_start_frame()
//    which overwrites the stack region previously occupied by Start()'s
//    local frame. Because both Start() and poison_former_start_frame() are
//    direct callees of the test body, they share the same call depth and thus
//    the same stack region.
// 3. Sleep for 50 ms so the detached probe thread fires while the stack has
//    already been poisoned (> 5 ms flush_interval).
// 4. Assert that SetQueueDepth received the expected value (probe_label.size()
//    for the known salt). If the stack was poisoned, the std::string
//    _M_string_length field reads as 0xCCCC... and the assertion fails.
//
// DETECTION UNDER AddressSanitizer
// ----------------------------------
// ASan "poisons" stack frames on return. The probe thread's access to the
// destroyed probe_label triggers an immediate "stack-use-after-return" error,
// crashing the test before assertions are reached. This is fully deterministic.
//
//   cmake .. -DCMAKE_BUILD_TYPE=Debug \
//            -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
//            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
//   cmake --build . --target analytics__16_analytics_worker_start_probe_uaf -j4
//   ./analytics__16_analytics_worker_start_probe_uaf
// ---------------------------------------------------------------------------

#define BOOST_TEST_MODULE AnalyticsWorkerStartProbeUAF
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <thread>

#include "url_shortener/analytics/AnalyticsWorker.hpp"

using namespace url_shortener::analytics;

namespace {

// Minimal metrics implementation that records the last SetQueueDepth value.
struct CountingMetrics final : IAnalyticsMetrics {
    std::atomic<int>      depth_call_count{0};
    std::atomic<uint64_t> last_depth{UINT64_MAX};

    void OnEnqueued() noexcept override {}
    void OnDropped() noexcept override {}
    void OnPersisted(std::uint64_t) noexcept override {}
    void OnWorkerFailure() noexcept override {}
    void ObserveEnqueueLatencyUs(std::uint64_t) noexcept override {}

    void SetQueueDepth(std::uint64_t d) noexcept override
    {
        ++depth_call_count;
        last_depth.store(d, std::memory_order_relaxed);
    }
};

struct NullRepo final : IClickEventRepository {
    bool InsertBatch(const std::vector<ClickEvent>&, AnalyticsError*) override { return true; }
    bool GetAggregateStats(const AggregateQuery&, AggregateStats*, AnalyticsError*) override { return true; }
    bool DeleteOlderThan(Timestamp, AnalyticsError*) override { return true; }
};

// Overwrites the current call frame (which occupies the same stack region as
// the just-returned Start() frame) with a known poison byte (0xCC).
// Must not be inlined so it creates its own activation record.
[[gnu::noinline]] void poison_former_start_frame()
{
    volatile unsigned char buf[512];
    std::memset(const_cast<unsigned char*>(buf), 0xCC, sizeof(buf));
    (void)buf;
}

} // namespace

// ---------------------------------------------------------------------------
// Case 1: probe_label captured by value (correct) — SetQueueDepth receives the
// exact expected size. Test PASSES.
//
// Case 2: probe_label captured by reference (the injected bug) — after
// poison_former_start_frame() overwrites the former stack frame, the detached
// thread reads 0xCC... bytes through the dangling reference. SetQueueDepth
// receives garbage (≠ expected_depth). BOOST_TEST assertion FAILS.
//
// Case 3: AddressSanitizer enabled — the test process crashes with
// "stack-use-after-return" before assertions are reached. Test FAILS.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(start_probe_must_not_hold_dangling_stack_reference)
{
    // Known salt → known probe_label string → known expected size.
    // "analytics:probe:fix:init" == 24 chars (above libstdc++ SSO threshold of 15,
    // so the string is heap-allocated in all major implementations).
    const std::string known_salt = "fix";
    const auto expected_depth = static_cast<uint64_t>(
        std::string("analytics:probe:").size() +
        known_salt.size() +
        std::string(":init").size());  // == 24

    AnalyticsConfig cfg;
    cfg.enabled          = true;
    cfg.batch_size       = 1;
    cfg.flush_interval   = std::chrono::milliseconds(5);  // probe thread sleeps 5 ms
    cfg.client_hash_salt = known_salt;
    cfg.retry.retry_backoff = std::chrono::milliseconds(0);

    BoundedClickEventQueue q(10);
    NullRepo               r;
    CountingMetrics        m;

    {
        AnalyticsWorker w(cfg, q, r, m);

        w.Start();
        // probe_label is DEAD here — Start() has returned and its stack frame is freed.

        // Overwrite the former Start() frame: same call depth, same stack region.
        // This must execute before the probe thread wakes up (5 ms from now).
        poison_former_start_frame();

        // Wait longer than flush_interval so the probe thread fires while the
        // former frame is poisoned.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        w.Stop();
    }

    // Probe thread must have called SetQueueDepth exactly once.
    BOOST_TEST(m.depth_call_count.load() == 1);

    // If the bug is present, the probe thread read 0xCC... from the destroyed
    // stack frame and last_depth != expected_depth → this assertion FAILS.
    // If the bug is absent (probe_label captured by value), last_depth == expected_depth.
    BOOST_TEST(m.last_depth.load() == expected_depth);
}
