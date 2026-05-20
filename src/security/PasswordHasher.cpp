#include "url_shortener/security/PasswordHasher.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

constexpr int         kSaltBytes  = 16;
constexpr int         kHashBytes  = 32;
constexpr int         kIterations = 100000;
constexpr const char* kAlgoTag    = "pbkdf2sha256";

std::string toHex(const unsigned char* data, std::size_t len)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return oss.str();
}

std::vector<unsigned char> fromHex(const std::string& hex)
{
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument("Invalid hex string");
    }
    std::vector<unsigned char> out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        unsigned val = 0;
        std::istringstream(hex.substr(i * 2, 2)) >> std::hex >> val;
        out[i] = static_cast<unsigned char>(val);
    }
    return out;
}

} // namespace

std::string PasswordHasher::hashPassword(const std::string& password) const
{
    unsigned char salt[kSaltBytes];
    if (RAND_bytes(salt, kSaltBytes) != 1) {
        throw std::runtime_error("Failed to generate random salt");
    }

    unsigned char hash[kHashBytes];
    if (PKCS5_PBKDF2_HMAC(password.data(),
                           static_cast<int>(password.size()),
                           salt, kSaltBytes,
                           kIterations,
                           EVP_sha256(),
                           kHashBytes,
                           hash) != 1) {
        throw std::runtime_error("PBKDF2 hashing failed");
    }

    // Format: algo:iterations:saltHex:hashHex
    return std::string(kAlgoTag) + ":"
        + std::to_string(kIterations) + ":"
        + toHex(salt, kSaltBytes) + ":"
        + toHex(hash, kHashBytes);
}

bool PasswordHasher::verifyPassword(const std::string& password,
                                    const std::string& passwordHash) const
{
    // Parse "algo:iterations:saltHex:hashHex"
    auto p1 = passwordHash.find(':');
    if (p1 == std::string::npos) return false;
    auto p2 = passwordHash.find(':', p1 + 1);
    if (p2 == std::string::npos) return false;
    auto p3 = passwordHash.find(':', p2 + 1);
    if (p3 == std::string::npos) return false;

    const std::string algo       = passwordHash.substr(0, p1);
    const int         iterations = std::stoi(passwordHash.substr(p1 + 1, p2 - p1 - 1));
    const std::string saltHex    = passwordHash.substr(p2 + 1, p3 - p2 - 1);
    const std::string hashHex    = passwordHash.substr(p3 + 1);

    if (algo != kAlgoTag) return false;

    const auto salt         = fromHex(saltHex);
    const auto expectedHash = fromHex(hashHex);

    std::vector<unsigned char> computed(expectedHash.size());
    if (PKCS5_PBKDF2_HMAC(password.data(),
                           static_cast<int>(password.size()),
                           salt.data(), static_cast<int>(salt.size()),
                           iterations,
                           EVP_sha256(),
                           static_cast<int>(computed.size()),
                           computed.data()) != 1) {
        return false;
    }

    // Constant-time comparison to prevent timing attacks
    if (computed.size() != expectedHash.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < computed.size(); ++i) {
        diff |= computed[i] ^ expectedHash[i];
    }
    return diff == 0;
}
