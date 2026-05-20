#include "url_shortener/security/ServiceClient.hpp"

#include <algorithm>

bool ServiceClient::hasScope(const std::string& scope) const
{
    return std::find(allowedScopes.begin(), allowedScopes.end(), scope) != allowedScopes.end();
}
