#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <regex>
#include <string>

/**
 * @brief Input model used when creating a short-link metadata record.
 */
struct CreateLinkRequest {
    std::string short_code;
    std::string target_url;
    std::optional<std::chrono::system_clock::time_point> expires_at;
    std::optional<std::string> owner;
    std::map<std::string, std::string> attributes;
    bool is_active = true;

    /**
     * @brief Validates request-level constraints shared by all backends.
     */
    bool isValid() const
    {
        static const std::regex short_code_regex("^[A-Za-z0-9_-]{3,64}$");
        return !target_url.empty() && std::regex_match(short_code, short_code_regex);
    }
};
