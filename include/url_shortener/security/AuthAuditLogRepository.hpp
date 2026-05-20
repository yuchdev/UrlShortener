#pragma once

#include <optional>
#include <string>

#include "url_shortener/security/AuthAuditLog.hpp"

namespace soci { class session; }

/**
 * @brief Append-only persistence layer for auth_audit_log.
 */
class AuthAuditLogRepository {
public:
    explicit AuthAuditLogRepository(soci::session& db);

    void append(const std::string& action,
                const std::string& result,
                const std::string* username  = nullptr,
                const std::string* userId    = nullptr,
                const std::string* clientId  = nullptr,
                const std::string* reason    = nullptr);

private:
    soci::session& db_;
};
