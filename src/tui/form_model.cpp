#include "url_shortener/tui/form_model.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/field_parsers.hpp>
#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

namespace
{

/// The advisory message pointing an operator at the JSON escape hatch when a
/// list/map value contains a `,`/`=` and cannot be edited in the comma form.
constexpr char k_use_json_form[] = "use the JSON form (palette -> cmd)";

/// Trims leading and trailing JSON whitespace so a raw value can be classified
/// by its first significant byte.
std::string_view trim_json_ws(const std::string& s)
{
    const auto is_ws = [](char c)
    { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && is_ws(s[begin])) {
        ++begin;
    }
    while (end > begin && is_ws(s[end - 1])) {
        --end;
    }
    return std::string_view(s).substr(begin, end - begin);
}

/// Whether @p raw, trimmed, is a JSON object (`{...}`) usable as a raw-object
/// field value.
bool looks_like_object(const std::string& raw)
{
    const std::string_view trimmed = trim_json_ws(raw);
    return !trimmed.empty() && trimmed.front() == '{' && trimmed.back() == '}';
}

/// Decodes one raw JSON string token (quotes included) into its UTF-8 value by
/// wrapping it in a throwaway object and reading it through the sanctioned
/// scanner (contract C9: no local JSON tokenizing). Returns the empty string
/// when @p token is not a decodable string.
std::string decode_json_string(const std::string& token)
{
    const std::string wrapped = "{\"v\":" + token + "}";
    return get_string(wrapped, "v").value_or(std::string());
}

}  // namespace

FormModel::FormModel(const CommandInfo& command, FormOptions options)
    : m_command_(command), m_options_(std::move(options))
{
    m_fields_.reserve(m_command_.args.size());
    for (const ArgInfo& spec : m_command_.args) {
        FormField field;
        field.spec = spec;
        field.members_text.assign(spec.members.size(), std::string());
        m_fields_.push_back(std::move(field));
    }
}

const CommandInfo& FormModel::command() const
{
    return m_command_;
}

std::vector<FormField>& FormModel::fields()
{
    return m_fields_;
}

const std::vector<FormField>& FormModel::fields() const
{
    return m_fields_;
}

void FormModel::set_text(std::size_t i, std::string text)
{
    if (i >= m_fields_.size()) {
        return;
    }
    FormField& field = m_fields_[i];
    if (field.locked) {
        return;
    }
    field.text = std::move(text);
    if (field.text.empty() && !field.spec.required) {
        field.state = FieldState::untouched;
    }
    else {
        field.state = FieldState::set;
    }
}

void FormModel::set_checked(std::size_t i, bool v)
{
    if (i >= m_fields_.size()) {
        return;
    }
    FormField& field = m_fields_[i];
    if (field.locked) {
        return;
    }
    field.checked = v;
    field.state = FieldState::set;
}

void FormModel::set_choice(std::size_t i, int index)
{
    if (i >= m_fields_.size()) {
        return;
    }
    FormField& field = m_fields_[i];
    if (field.locked) {
        return;
    }
    if (index < 0) {
        field.choice = -1;
        field.state = FieldState::untouched;
    }
    else {
        field.choice = index;
        field.state = FieldState::set;
    }
}

void FormModel::set_member(std::size_t i, std::size_t m, std::string text)
{
    if (i >= m_fields_.size()) {
        return;
    }
    FormField& field = m_fields_[i];
    if (field.locked || m >= field.members_text.size()) {
        return;
    }
    field.members_text[m] = std::move(text);
    bool any = false;
    for (const std::string& member : field.members_text) {
        if (!member.empty()) {
            any = true;
            break;
        }
    }
    field.state = (any || field.spec.required) ? FieldState::set
                                               : FieldState::untouched;
}

bool FormModel::clear_field(std::size_t i)
{
    if (i >= m_fields_.size()) {
        return false;
    }
    FormField& field = m_fields_[i];
    if (!field.spec.nullable || field.locked) {
        return false;
    }
    field.state = FieldState::cleared;
    return true;
}

void FormModel::set_dry_run(bool v)
{
    m_dry_run_ = v;
}

bool FormModel::dry_run() const
{
    return m_dry_run_;
}

std::optional<std::string> FormModel::encode_field(const FormField& field) const
{
    if (field.state == FieldState::untouched) {
        return std::nullopt;
    }
    if (field.state == FieldState::cleared) {
        return std::string("null");
    }

    // field.state == FieldState::set
    switch (field.spec.kind) {
        case ArgKind::integer: {
            const std::optional<long long> value = parse_integer(field.text);
            if (value.has_value()) {
                return std::to_string(*value);
            }
            // Keep the envelope well-formed; the server returns invalid_field.
            return url_shortener::jsonString(field.text);
        }

        case ArgKind::boolean:
            return std::string(field.checked ? "true" : "false");

        case ArgKind::enumeration: {
            if (field.choice >= 0
                && static_cast<std::size_t>(field.choice)
                    < field.spec.enum_values.size()) {
                return url_shortener::jsonString(
                    field.spec.enum_values[
                        static_cast<std::size_t>(field.choice)]);
            }
            return std::nullopt;
        }

        case ArgKind::string_list: {
            const std::vector<std::string> items =
                parse_string_list(field.text);
            std::string out = "[";
            bool first = true;
            for (const std::string& item : items) {
                if (!first) {
                    out += ",";
                }
                first = false;
                out += url_shortener::jsonString(item);
            }
            out += "]";
            return out;
        }

        case ArgKind::string_map: {
            const auto pairs = parse_string_map(field.text);
            std::string out = "{";
            if (pairs.has_value()) {
                bool first = true;
                for (const auto& pair : *pairs) {
                    if (!first) {
                        out += ",";
                    }
                    first = false;
                    out += url_shortener::jsonString(pair.first);
                    out += ":";
                    out += url_shortener::jsonString(pair.second);
                }
            }
            out += "}";
            return out;
        }

        case ArgKind::object: {
            if (field.spec.members.empty()) {
                // Raw-JSON object field: emit verbatim only when it is a JSON
                // object, else omit so the envelope cannot be corrupted.
                if (looks_like_object(field.text)) {
                    return std::string(trim_json_ws(field.text));
                }
                return std::nullopt;
            }
            std::string out = "{";
            bool first = true;
            for (std::size_t m = 0; m < field.spec.members.size(); ++m) {
                const std::string& value = (m < field.members_text.size())
                    ? field.members_text[m]
                    : std::string();
                if (value.empty()) {
                    continue;
                }
                if (!first) {
                    out += ",";
                }
                first = false;
                out += url_shortener::jsonString(field.spec.members[m]);
                out += ":";
                out += url_shortener::jsonString(value);
            }
            out += "}";
            if (first) {
                return std::nullopt;  // all members empty -> omit entirely
            }
            return out;
        }

        case ArgKind::string:
        case ArgKind::timestamp:
        default:
            // string, timestamp, and any future/unknown kind degrade to a
            // JSON string field.
            return url_shortener::jsonString(field.text);
    }
}

std::string FormModel::to_args_json() const
{
    std::string out = "{";
    bool first = true;
    for (const FormField& field : m_fields_) {
        const std::optional<std::string> value = encode_field(field);
        if (!value.has_value()) {
            continue;
        }
        if (!first) {
            out += ",";
        }
        first = false;
        out += url_shortener::jsonString(field.spec.name);
        out += ":";
        out += *value;
    }
    out += "}";
    return out;
}

commands::CommandRequest FormModel::to_request(
    const std::string& request_id) const
{
    commands::CommandRequest request;
    request.v = 1;
    request.id = request_id;
    request.command = m_command_.name;
    request.args_json = to_args_json();
    request.options.dry_run = m_dry_run_
        && m_options_.include_dry_run_toggle && m_command_.supports_dry_run;
    return request;
}

bool FormModel::validate()
{
    bool ok = true;
    for (FormField& field : m_fields_) {
        field.error.reset();

        if (field.spec.required && field.state == FieldState::untouched) {
            field.error = "required";
            ok = false;
            continue;
        }
        if (field.state != FieldState::set) {
            continue;  // untouched optional / explicit null needs no check
        }

        switch (field.spec.kind) {
            case ArgKind::integer:
                if (!parse_integer(field.text).has_value()) {
                    field.error = "not a whole number";
                    ok = false;
                }
                break;

            case ArgKind::enumeration:
                if (field.choice < 0
                    || static_cast<std::size_t>(field.choice)
                        >= field.spec.enum_values.size()) {
                    field.error = "not a valid choice";
                    ok = false;
                }
                break;

            case ArgKind::timestamp:
                if (!url_shortener::parseRfc3339Zulu(field.text).has_value()) {
                    field.error = "expected YYYY-MM-DDThh:mm:ssZ";
                    ok = false;
                }
                break;

            case ArgKind::string_map:
                if (!parse_string_map(field.text).has_value()) {
                    field.error = "expected key=value pairs";
                    ok = false;
                }
                break;

            case ArgKind::object:
                if (field.spec.members.empty()
                    && !looks_like_object(field.text)) {
                    field.error = "expected a JSON object";
                    ok = false;
                }
                break;

            case ArgKind::string:
            case ArgKind::boolean:
            case ArgKind::string_list:
            default:
                break;  // no cheap client-side check
        }
    }
    return ok;
}

std::string FormModel::validation_summary() const
{
    std::string out;
    bool first = true;
    for (const FormField& field : m_fields_) {
        if (field.state == FieldState::untouched) {
            continue;
        }
        if (!first) {
            out += " - ";
        }
        first = false;
        out += field.spec.name;
        if (field.error.has_value()) {
            out += ": ";
            out += *field.error;
        }
        else {
            out += " ok";
        }
    }
    return out;
}

void FormModel::prefill_from_result(const std::string& result_json)
{
    for (FormField& field : m_fields_) {
        const std::string& name = field.spec.name;
        bool got_value = false;

        switch (field.spec.kind) {
            case ArgKind::integer: {
                const auto value = get_int(result_json, name);
                if (value.has_value()) {
                    field.text = std::to_string(*value);
                    got_value = true;
                }
                break;
            }

            case ArgKind::boolean: {
                const auto value = get_bool(result_json, name);
                if (value.has_value()) {
                    field.checked = *value;
                    got_value = true;
                }
                break;
            }

            case ArgKind::enumeration: {
                const auto value = get_string(result_json, name);
                if (value.has_value()) {
                    for (std::size_t e = 0; e < field.spec.enum_values.size();
                         ++e) {
                        if (field.spec.enum_values[e] == *value) {
                            field.choice = static_cast<int>(e);
                            got_value = true;
                            break;
                        }
                    }
                }
                break;
            }

            case ArgKind::string_list: {
                const auto raw = raw_field(result_json, name);
                if (raw.has_value()) {
                    std::vector<std::string> items;
                    for (const std::string& token : split_array(*raw)) {
                        items.push_back(decode_json_string(token));
                    }
                    bool representable = true;
                    field.text = format_string_list(items, representable);
                    if (!representable) {
                        field.error = k_use_json_form;
                    }
                    got_value = true;
                }
                break;
            }

            case ArgKind::string_map: {
                const auto raw = raw_field(result_json, name);
                if (raw.has_value()) {
                    std::vector<std::pair<std::string, std::string>> pairs;
                    for (const JsonField& member : object_fields(*raw)) {
                        pairs.emplace_back(
                            member.key, decode_json_string(member.raw_value));
                    }
                    bool representable = true;
                    field.text = format_string_map(pairs, representable);
                    if (!representable) {
                        field.error = k_use_json_form;
                    }
                    got_value = true;
                }
                break;
            }

            case ArgKind::object: {
                const auto raw = raw_field(result_json, name);
                if (raw.has_value()) {
                    if (field.spec.members.empty()) {
                        field.text = *raw;
                    }
                    else {
                        for (const JsonField& member : object_fields(*raw)) {
                            for (std::size_t m = 0;
                                 m < field.spec.members.size(); ++m) {
                                if (field.spec.members[m] == member.key) {
                                    field.members_text[m] =
                                        decode_json_string(member.raw_value);
                                    break;
                                }
                            }
                        }
                    }
                    got_value = true;
                }
                break;
            }

            case ArgKind::string:
            case ArgKind::timestamp:
            default: {
                const auto value = get_string(result_json, name);
                if (value.has_value()) {
                    field.text = *value;
                    got_value = true;
                }
                break;
            }
        }

        if (got_value && field.spec.required) {
            // The identity field (e.g. the slug of an update): shown read-only
            // and re-sent, so the request addresses the right resource.
            field.state = FieldState::set;
            field.locked = true;
        }
        else {
            // Prefilled but untouched, so an unchanged field is not re-sent
            // (PATCH semantics).
            field.state = FieldState::untouched;
        }
    }
}

}  // namespace url_shortener::tui
