#include "url_shortener/analytics/ClientIdHasher.hpp"

#include <openssl/hmac.h>

#include <iomanip>
#include <sstream>

namespace url_shortener::analytics {

std::string ClientIdHasher::Hash(const std::string& normalized_identifier, const ClientIdHashConfig& config, AnalyticsError* error)
{
    if (config.salt.empty()) {
        if (error) { error->code = AnalyticsErrorCode::validation; error->message = "Missing salt"; }
        return {};
    }
    if (normalized_identifier.empty()) {
        if (config.allow_missing_identifier) return {};
        if (error) { error->code = AnalyticsErrorCode::validation; error->message = "Missing identifier"; }
        return {};
    }
    unsigned int len = 0;
    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    HMAC(EVP_sha256(), config.salt.data(), static_cast<int>(config.salt.size()),
         reinterpret_cast<const unsigned char*>(normalized_identifier.data()), normalized_identifier.size(), digest, &len);

    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; ++i) os << std::setw(2) << static_cast<int>(digest[i]);
    auto out = os.str();
    if (config.output_length > 0 && out.size() > config.output_length) out.resize(config.output_length);
    return out;
}

} // namespace url_shortener::analytics
