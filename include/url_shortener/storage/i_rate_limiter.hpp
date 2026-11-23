#pragma once

#include <chrono>
#include <string>

#include "url_shortener/storage/errors/rate_limit_error.hpp"
#include "url_shortener/storage/models/rate_limit_decision.hpp"

/**
 * @brief Rate-limiter port for per-key fixed-window request throttling
 * (see `CLAUDE.md` "Storage: ports and adapters"). Implementations
 * are fail-open by convention: a backend failure should return a
 * permissive decision rather than blocking the request (see
 * `docs/storage/overview.md` "Failure semantics summary").
 *
 * Thread-safety: implementations must be safe to call concurrently
 * from multiple request-handling threads.
 */
class IRateLimiter
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed
     * through a base `IRateLimiter*`.
     */
    virtual ~IRateLimiter() = default;

    /**
     * @brief Evaluates whether the caller identified by `key` may
     * proceed under a fixed-window policy of `limit` requests per
     * `window`.
     * @param key Identifier for the rate-limited caller (e.g. client
     * IP, API key). Must be non-empty; an empty key causes an
     * immediate deny with `RateLimitError::invalid_key`.
     * @param limit Maximum number of requests permitted per `window`.
     * A value of zero denies every request (and sets
     * `RateLimitDecision::allowed` to `false`) without setting an
     * error.
     * @param window Length of the sliding window; must be positive.
     * A zero or negative value causes an immediate deny with
     * `RateLimitError::invalid_policy`.
     * @param[out] error If non-null and a precondition or backend
     * error occurs, receives the failure reason; left untouched
     * when the decision is based on a normal limit check (even
     * if the request is denied).
     * @return A `RateLimitDecision` with `allowed` set to `true` if
     * the caller is within the limit, `false` if the limit is
     * exceeded, the key/policy is invalid, or the backend failed.
     * Check `error` to distinguish a normal deny from a failure.
     */
    virtual RateLimitDecision Allow(const std::string& key,
                                    uint64_t limit,
                                    std::chrono::seconds window,
                                    RateLimitError* error = nullptr) = 0;
};
