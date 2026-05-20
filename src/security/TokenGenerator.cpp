#include "url_shortener/security/TokenGenerator.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

std::string toHex(const unsigned char* data, std::size_t len)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return oss.str();
}

} // namespace

std::string TokenGenerator::generateToken(std::size_t byteLength) const
{
    std::vector<unsigned char> buf(byteLength);
    if (RAND_bytes(buf.data(), static_cast<int>(byteLength)) != 1) {
        throw std::runtime_error("Failed to generate random token");
    }
    return toHex(buf.data(), byteLength);
}

std::string TokenGenerator::hashToken(const std::string& rawToken) const
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(rawToken.data()),
           rawToken.size(),
           digest);
    return toHex(digest, SHA256_DIGEST_LENGTH);
}
