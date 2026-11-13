#pragma once

#include <string>
#include <vector>

#include <url_shortener/observability/logger.hpp>

namespace url_shortener::commands
{

/**
 * @brief One audit line for an attempted execution of a mutating command
 * (milestone contracts C6/C7, threat model T-14/T-15).
 *
 * Emitted once per attempt by @ref CommandRegistry::execute through an
 * @ref IAuditSink, for every outcome a mutating command can reach (success,
 * denial, invalid arguments, unsupported mode, a full queue, an accepted job,
 * or an internal failure). It records who did what to which target and how it
 * ended; it deliberately carries **no argument values and no raw client
 * identifiers** (contract C6), so the whole record is safe to place in the
 * application log. Every field is neutralized by @ref audit_fields before it
 * reaches a sink, so even a hostile slug or principal cannot inject a log line.
 */
struct AuditRecord
{
    ///< Caller identity label copied from @ref CallerContext::principal, e.g.
    ///< `"local-operator"`; a diagnostic label, never an argument value.
    std::string principal;

    ///< Originating transport copied from @ref CallerContext::transport, e.g.
    ///< `"rest"`, `"cli"`, `"tui"`, `"stdio"`.
    std::string transport;

    ///< The resolved command name, e.g. `"link.delete"`; the descriptor's
    ///< validated name, never raw caller input.
    std::string command;

    ///< The affected slug / id / job id, taken from a validated
    ///< @ref CommandResult::audit_target or an accepted job id; empty when the
    ///< attempt produced no target (a denial or invalid-argument attempt).
    ///< Length-capped to 64 bytes by the registry before emission; never an
    ///< argument value.
    std::string target;

    ///< How the attempt ended: `"ok"`, `"dry_run"`, `"job_accepted"`, or the
    ///< wire name of the @ref CommandErrorCode it failed with (e.g.
    ///< `"not_authorized"`, `"invalid_field"`, `"internal"`).
    std::string outcome;
};

/**
 * @brief The sink the command registry hands one @ref AuditRecord per mutating
 * execution (decision log; threat model T-14).
 *
 * Implementations persist or forward the record (the production
 * @ref LoggerAuditSink writes it to the application log). @ref record is
 * `noexcept`: a sink must never throw, because the registry emits the record
 * after computing the response and an exception must not alter that response.
 * Exactly one process-wide sink is installed per mode in composition (contract
 * C11); unit tests may substitute a capturing fake.
 */
class IAuditSink
{
public:
    /**
     * @brief Virtual destructor; the registry holds a `std::shared_ptr` to a
     * sink through this base.
     */
    virtual ~IAuditSink() = default;

    /**
     * @brief Records one audit line for a mutating command attempt.
     *
     * Called by @ref CommandRegistry::execute exactly once per attempt at a
     * mutating command, after the response is known. Must not throw: the
     * `noexcept` contract guarantees auditing cannot change the command
     * response (threat model T-14).
     *
     * @param record The audit line to record; its fields are already
     * sanitized-on-emission by @ref audit_fields in the production sink, but an
     * implementation that serializes the record itself MUST route its text
     * through @ref audit_fields (or @ref sanitize_error_message) first.
     */
    virtual void record(const AuditRecord& record) noexcept = 0;
};

/**
 * @brief Converts an @ref AuditRecord into the structured log fields of a
 * `command_audit` line, neutralizing every field for safe emission.
 *
 * Pure and testable: it returns the fixed field set
 * `{event:"command_audit", principal, transport, command, target, outcome}` in
 * that order. Every value field is passed through
 * @ref sanitize_error_message, which (threat model T-15/T-03) replaces control
 * characters (C0/DEL/C1, including NUL), replaces invalid-UTF-8 bytes (e.g. a
 * bare `0x80` continuation byte) with `'?'`, and truncates to at most 128 bytes
 * without ever splitting a multi-byte character. No raw control, invalid-UTF-8
 * or NUL byte can therefore reach the logger or any JSON log consumer.
 *
 * @param record The record to render; may hold any bytes (it is sanitized
 * here).
 * @return The log fields for a `command_audit` line: the literal `event` key
 * plus the five sanitized record fields, each valid UTF-8 of at most 128 bytes.
 */
std::vector<observability::LogField> audit_fields(const AuditRecord& record);

/**
 * @brief The production @ref IAuditSink: writes each record to the application
 * log as a `command_audit` info line (Task 03.0 subtask 18 installs it in
 * composition).
 *
 * Holds its own @ref observability::Logger by value (the logger is cheaply
 * copyable and typically carries the `"audit"` component label). The sink works
 * whether or not the global logger was initialized; when it was not, the
 * underlying `Logger::info` simply drops the line. @ref record is `noexcept`
 * and additionally wraps the logging call in `catch (...)` as defense in depth.
 */
class LoggerAuditSink final : public IAuditSink
{
public:
    /**
     * @brief Constructs the sink around a logger instance.
     *
     * @param logger The logger to write `command_audit` lines through; copied
     * and owned by value (typically `Logger("audit")`). No reference to
     * external state is retained.
     */
    explicit LoggerAuditSink(observability::Logger logger);

    /**
     * @brief Writes @p record to the application log as a `command_audit` info
     * line built by @ref audit_fields.
     *
     * Never throws: the logging call is wrapped in `catch (...)` so a logging
     * failure is swallowed and the command response is unaffected (threat model
     * T-14).
     *
     * @param record The audit line to log; its fields are neutralized by
     * @ref audit_fields before emission.
     */
    void record(const AuditRecord& record) noexcept override;

private:
    ///< The logger the sink emits `command_audit` lines through; owned by
    ///< value, so the sink holds no reference to external state.
    observability::Logger m_logger;
};

}  // namespace url_shortener::commands
