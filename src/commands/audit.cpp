#include "url_shortener/commands/audit.hpp"

#include <string>
#include <utility>
#include <vector>

#include "url_shortener/commands/error_codes.hpp"

namespace url_shortener::commands
{

std::vector<observability::LogField> audit_fields(const AuditRecord& record)
{
    // Every value is routed through sanitize_error_message: it caps each field
    // to 128 bytes, replaces control characters (C0/DEL/C1, including NUL) and
    // invalid-UTF-8 bytes with '?', and never splits a multi-byte character
    // (threat model T-15/T-03). The `event` key/value is fixed text.
    return {
        observability::LogField {"event", "command_audit"},
        observability::LogField {"principal",
                                 sanitize_error_message(record.principal)},
        observability::LogField {"transport",
                                 sanitize_error_message(record.transport)},
        observability::LogField {"command",
                                 sanitize_error_message(record.command)},
        observability::LogField {"target",
                                 sanitize_error_message(record.target)},
        observability::LogField {"outcome",
                                 sanitize_error_message(record.outcome)},
    };
}

LoggerAuditSink::LoggerAuditSink(observability::Logger logger)
    : m_logger(std::move(logger))
{
}

void LoggerAuditSink::record(const AuditRecord& record) noexcept
{
    // noexcept contract (threat model T-14): a logging failure must never
    // propagate and alter the command response, so swallow everything.
    try
    {
        m_logger.info("command_audit", audit_fields(record));
    }
    catch (...)
    {
    }
}

}  // namespace url_shortener::commands
