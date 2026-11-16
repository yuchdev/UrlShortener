#include "url_shortener/commands/args_builder.hpp"

#include <stdexcept>
#include <string>

#include "url_shortener/core/utils.hpp"

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Whether @p name matches the catalog argument-name grammar
 * `[a-z][a-z0-9_]*`.
 *
 * @param name Candidate member name.
 * @return `true` if non-empty, starts with a lowercase ASCII letter and every
 * following character is a lowercase ASCII letter, digit or underscore.
 */
bool is_valid_name(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    const char first = name.front();
    if (first < 'a' || first > 'z') {
        return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

args_builder& args_builder::add_raw(std::string_view name,
                                    std::string_view json_token)
{
    if (!is_valid_name(name)) {
        // Programming error: the error text names only the key, never a value.
        throw std::invalid_argument("args_builder: invalid member name '"
                                    + std::string(name) + "'");
    }
    if (has(name)) {
        throw std::invalid_argument("args_builder: duplicate member name '"
                                    + std::string(name) + "'");
    }
    m_members.emplace_back(std::string(name), std::string(json_token));
    return *this;
}

args_builder& args_builder::add_string(std::string_view name,
                                       std::string_view value)
{
    return add_raw(name, jsonString(std::string(value)));
}

args_builder& args_builder::add_bool(std::string_view name, bool value)
{
    return add_raw(name, value ? "true" : "false");
}

args_builder& args_builder::add_int(std::string_view name, long long value)
{
    return add_raw(name, std::to_string(value));
}

args_builder& args_builder::add_null(std::string_view name)
{
    return add_raw(name, "null");
}

args_builder& args_builder::add_string_list(
    std::string_view name, const std::vector<std::string>& values)
{
    std::string token = "[";
    bool first = true;
    for (const std::string& value : values) {
        if (!first) {
            token += ',';
        }
        first = false;
        token += jsonString(value);
    }
    token += ']';
    return add_raw(name, token);
}

args_builder& args_builder::add_string_map(
    std::string_view name, const std::map<std::string, std::string>& values)
{
    // std::map iterates in sorted key order, giving deterministic text.
    std::string token = "{";
    bool first = true;
    for (const auto& [key, value] : values) {
        if (!first) {
            token += ',';
        }
        first = false;
        token += jsonString(key);
        token += ':';
        token += jsonString(value);
    }
    token += '}';
    return add_raw(name, token);
}

bool args_builder::empty() const
{
    return m_members.empty();
}

bool args_builder::has(std::string_view name) const
{
    for (const auto& member : m_members) {
        if (member.first == name) {
            return true;
        }
    }
    return false;
}

std::string args_builder::str() const
{
    if (m_members.empty()) {
        return "{}";
    }
    std::string out = "{";
    bool first = true;
    for (const auto& [name, value] : m_members) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += jsonString(name);
        out += ':';
        out += value;
    }
    out += '}';
    return out;
}

}  // namespace url_shortener::commands
