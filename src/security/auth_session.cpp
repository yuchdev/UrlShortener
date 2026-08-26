#include "url_shortener/security/auth_session.hpp"

bool AuthSession::isActive(std::chrono::system_clock::time_point now) const
{
    if (revokedAt.has_value()) return false;
    if (now >= expiresAt)      return false;
    return true;
}
