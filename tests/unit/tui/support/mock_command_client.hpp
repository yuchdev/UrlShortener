#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/command_client.hpp>

namespace url_shortener::tui::testing
{

/**
 * @brief A scripted, recording @ref ICommandClient for screen and async tests.
 *
 * Lets a test drive a screen with no registry and no storage (success criterion
 * 2): responses are pre-scripted per command name with @ref enqueue, every
 * @ref call is recorded in @ref requests, and @ref commands / @ref session are
 * fixed by @ref set_commands / @ref set_session. An optional per-call delay
 * (@ref set_delay) models a slow transport for the off-UI-thread tests of
 * subtask 03. Task 07.0 subtask 22 extends this one class; it does not add a
 * second mock.
 *
 * Thread-safety: @ref enqueue, @ref call and the accessors lock an internal
 * mutex, so a worker thread may @ref call while the test thread scripts or
 * reads state via @ref recorded_requests. The public @ref requests vector is
 * for single-threaded screen tests; read it only after any worker has joined.
 */
class MockCommandClient final : public ICommandClient
{
public:
    /**
     * @brief Scripts one response to be returned for the next @ref call of
     * @p command.
     *
     * Responses for the same command name are returned FIFO. A @ref call for a
     * command with no remaining scripted response yields an `internal` error
     * envelope (so a missing script surfaces loudly, not as a false success).
     *
     * @param command The command name this response answers.
     * @param response The response to return; moved into the script.
     */
    void enqueue(std::string command, commands::CommandResponse response)
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_scripted_[std::move(command)].push_back(std::move(response));
    }

    /**
     * @brief Sets the catalog @ref commands returns.
     *
     * @param commands_list The command catalog snapshot; moved in.
     */
    void set_commands(std::vector<CommandInfo> commands_list)
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_commands_ = std::move(commands_list);
    }

    /**
     * @brief Sets the session @ref session returns.
     *
     * @param session_info The session snapshot; moved in.
     */
    void set_session(SessionInfo session_info)
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_session_ = std::move(session_info);
    }

    /**
     * @brief Sets a delay applied at the start of every @ref call.
     *
     * @param delay How long each @ref call sleeps before returning; models a
     * slow transport for async tests. Zero (the default) never sleeps.
     */
    void set_delay(std::chrono::milliseconds delay)
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_delay_ = delay;
    }

    /**
     * @brief Installs a hook that emulates the registry's `permission_for`
     * argument-dependent permission raise (decision log D16).
     *
     * @ref required_permission consults @p hook first: a returned value is used
     * as the request's effective wire permission; `std::nullopt` falls back to
     * the base permission from the catalog set via @ref set_commands. This lets
     * a screen/matrix test prove the arg-aware read-only gate (e.g. `link.list`
     * with `status=all` raising to `write`) against the fake, matching how
     * @ref InProcessClient reads it from the real registry.
     *
     * @param hook The emulated raise; moved in. An empty hook restores the
     * base-permission default.
     */
    void set_permission_hook(std::function<std::optional<std::string>(
                                 const commands::CommandRequest&)> hook)
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        m_permission_hook_ = std::move(hook);
    }

    /**
     * @brief Returns a copy of every recorded request, taken under the lock.
     *
     * @return The recorded requests in call order; safe to call while a worker
     * thread is issuing calls.
     */
    std::vector<commands::CommandRequest> recorded_requests() const
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        return requests;
    }

    /**
     * @brief Records @p request and returns its scripted response.
     *
     * @param request The command request; recorded verbatim in @ref requests.
     * @return The next scripted response for `request.command`, or an
     * `internal` error envelope echoing `request.id` when none remains.
     */
    commands::CommandResponse call(
        const commands::CommandRequest& request) override
    {
        std::chrono::milliseconds delay {0};
        {
            std::lock_guard<std::mutex> lock(m_mutex_);
            requests.push_back(request);
            delay = m_delay_;
        }
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }

        std::lock_guard<std::mutex> lock(m_mutex_);
        auto it = m_scripted_.find(request.command);
        if (it == m_scripted_.end() || it->second.empty()) {
            return commands::make_failure(
                request.id,
                commands::make_error(commands::CommandErrorCode::internal,
                                     "no scripted response"));
        }
        commands::CommandResponse response = std::move(it->second.front());
        it->second.pop_front();
        return response;
    }

    /**
     * @brief Returns the scripted command catalog.
     *
     * @return A copy of the catalog set by @ref set_commands (empty by
     * default).
     */
    std::vector<CommandInfo> commands() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        return m_commands_;
    }

    /**
     * @brief Returns the scripted session.
     *
     * @return A copy of the session set by @ref set_session (default-
     * constructed by default).
     */
    SessionInfo session() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        return m_session_;
    }

    /**
     * @brief Resolves @p request's effective permission, emulating the
     * registry's argument-dependent raise when a hook is installed.
     *
     * @param request The request whose effective permission to resolve.
     * @return The hook's value for @p request when it yields one; otherwise the
     * base permission of the matching catalog entry, or `"manage_users"` for an
     * unknown command (fail closed), mirroring the interface default.
     */
    std::string required_permission(
        const commands::CommandRequest& request) const override
    {
        std::lock_guard<std::mutex> lock(m_mutex_);
        if (m_permission_hook_) {
            const std::optional<std::string> hinted =
                m_permission_hook_(request);
            if (hinted.has_value()) {
                return *hinted;
            }
        }
        for (const CommandInfo& info : m_commands_) {
            if (info.name == request.command) {
                return info.permission;
            }
        }
        return "manage_users";
    }

    ///< Every request passed to @ref call, in order. For single-threaded
    ///< screen tests; across threads read it only after workers join, or use
    ///< @ref recorded_requests.
    std::vector<commands::CommandRequest> requests;

private:
    ///< Guards @ref requests and every scripted member so a worker thread may
    ///< @ref call while the test thread scripts or reads state.
    mutable std::mutex m_mutex_;

    ///< Scripted responses keyed by command name, each returned FIFO.
    std::map<std::string, std::deque<commands::CommandResponse>> m_scripted_;

    ///< The catalog @ref commands returns.
    std::vector<CommandInfo> m_commands_;

    ///< The session @ref session returns.
    SessionInfo m_session_;

    ///< Per-call delay applied by @ref call; zero disables sleeping.
    std::chrono::milliseconds m_delay_ {0};

    ///< Optional emulation of the registry's `permission_for` raise; consulted
    ///< by @ref required_permission before the catalog base permission.
    std::function<std::optional<std::string>(const commands::CommandRequest&)>
        m_permission_hook_;
};

}  // namespace url_shortener::tui::testing
