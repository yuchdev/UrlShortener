#pragma once

#include <chrono>

/**
 * @brief Clock abstraction used throughout the application to allow
 * deterministic time control in tests.
 *
 * Production code receives a `SystemClock`; test code receives a
 * `ManualClock` that can be advanced programmatically.
 */
class IClock
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed
     * through a base `IClock*`.
     */
    virtual ~IClock() = default;

    /**
     * @brief Returns the current time as seen by this clock.
     * @return A `system_clock::time_point` representing the current
     * (or simulated) wall-clock instant.
     */
    virtual std::chrono::system_clock::time_point now() const = 0;
};

/**
 * @brief Production `IClock` implementation backed by the system
 * wall clock.
 *
 * Delegates directly to `std::chrono::system_clock::now()`.
 * Thread-safety: `now()` is safe to call concurrently from any
 * thread.
 */
class SystemClock final : public IClock
{
public:
    /**
     * @brief Returns the current wall-clock time.
     * @return `std::chrono::system_clock::now()`.
     */
    std::chrono::system_clock::time_point now() const override
    {
        return std::chrono::system_clock::now();
    }
};

/**
 * @brief Test-only `IClock` implementation whose current time is
 * set and advanced programmatically.
 *
 * Inject into `LinkService` and storage adapters under test instead
 * of `SystemClock` to exercise time-dependent logic without sleeping.
 *
 * Thread-safety: none; `ManualClock` is intended for single-threaded
 * test use only.
 */
class ManualClock final : public IClock
{
public:
    /**
     * @brief Constructs a clock frozen at `initial`.
     * @param initial Starting time point returned by `now()` until
     * `set` or `advance` is called.
     */
    explicit ManualClock(std::chrono::system_clock::time_point initial)
        : now_(initial)
    {
    }

    /**
     * @brief Returns the simulated current time.
     * @return The time point last set by the constructor, `set`, or
     * `advance`.
     */
    std::chrono::system_clock::time_point now() const override { return now_; }

    /**
     * @brief Replaces the simulated current time with `value`.
     * @param value New time point to return from `now()`.
     */
    void set(std::chrono::system_clock::time_point value) { now_ = value; }

    /**
     * @brief Advances the simulated current time by `delta`.
     * @param delta Duration to add to the current simulated time;
     * may be negative to move the clock backwards.
     */
    void advance(std::chrono::seconds delta) { now_ += delta; }

private:
    ///< Simulated current time; mutable via `set` and `advance`.
    std::chrono::system_clock::time_point now_;
};
