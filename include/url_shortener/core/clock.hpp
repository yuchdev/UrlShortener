#pragma once

#include <chrono>

/**
 * @brief Clock abstraction for deterministic tests.
 */
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::system_clock::time_point now() const = 0;
};

/**
 * @brief Production clock implementation backed by wall time.
 */
class SystemClock final : public IClock {
public:
    std::chrono::system_clock::time_point now() const override
    {
        return std::chrono::system_clock::now();
    }
};

/**
 * @brief Test-only controllable clock implementation.
 */
class ManualClock final : public IClock {
public:
    explicit ManualClock(std::chrono::system_clock::time_point initial)
        : now_(initial)
    {
    }

    std::chrono::system_clock::time_point now() const override
    {
        return now_;
    }

    void set(std::chrono::system_clock::time_point value)
    {
        now_ = value;
    }

    void advance(std::chrono::seconds delta)
    {
        now_ += delta;
    }

private:
    std::chrono::system_clock::time_point now_;
};
