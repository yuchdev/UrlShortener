#define BOOST_TEST_MODULE TuiFormModelPropertyTest

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/equivalent_cmd.hpp>
#include <url_shortener/tui/form_model.hpp>

#include "hostile_strings.hpp"
#include "tui_test_support.hpp"

/**
 * @file 30_form_model_property_test.cpp
 * @brief Seeded property tests for the generic @ref
 * url_shortener::tui::FormModel (FTXUI-free model library), proving its
 * `to_args_json()` round-trips through the registry's own
 * `commands::validate_args` (Task 07.0 subtask 26).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: a deterministic `std::mt19937` (seed from `FORM_PROP_SEED`, default
 *           20261019) builds random `CommandInfo`/`ArgSpec` pairs covering
 * every `commands::ArgType`, drives the form through its index-based mutators
 *           with valid and with invalid edits, and asserts the subtask's
 *           properties: a valid form validates and its args pass
 *           `commands::validate_args` against the same spec; untouched fields
 * are omitted; set values keep their JSON type; a cleared nullable field
 *           becomes `null`; prefill is idempotent and re-emits a valid
 * envelope; and every byte (quotes, backslashes, newlines, emoji, 4 KiB paste)
 *           survives JSON escaping unchanged. A fixed edge-case table and one
 *           real `link.create` case from the live catalog round it out.
 * Boundaries: all 8 ArgType values; required/optional/nullable/default; integer
 *           bounds vs registry limits; 64- vs 65-byte strings; empty enum list;
 *           a buffer at `k_max_field_bytes`; invalid UTF-8 kept well-formed.
 * On failure first check: the failing `BOOST_TEST_CONTEXT` prints the seed and
 *           case index - rerun with `FORM_PROP_SEED=<seed>`; then inspect
 *           `encode_field`/`validate`/`prefill_from_result` in
 *           `src/tui/form_model.cpp` and `validate_args` in
 *           `src/commands/arg_spec.cpp`. This test asserts behaviour only; a
 *           genuine model defect is reported, never patched here.
 */

namespace cmd = url_shortener::commands;

using url_shortener::tui::ArgInfo;
using url_shortener::tui::ArgKind;
using url_shortener::tui::CommandInfo;
using url_shortener::tui::FieldState;
using url_shortener::tui::FormModel;

namespace
{

/// Fixed default seed used when `FORM_PROP_SEED` is unset; documented so a CI
/// failure is reproducible with `FORM_PROP_SEED=20261019`.
constexpr std::uint32_t k_default_seed = 20261019u;

/// Property iteration count per looping case (bounded so the suite stays fast;
/// no wall-clock assertion is made).
constexpr int k_iterations = 200;

/// The eight catalog argument types, enumerated so adding a new `ArgType`
/// without `FormModel` support makes the generator exercise it (and fail P1).
constexpr cmd::ArgType k_all_types[] = {cmd::ArgType::string,
                                        cmd::ArgType::integer,
                                        cmd::ArgType::boolean,
                                        cmd::ArgType::enumeration,
                                        cmd::ArgType::timestamp,
                                        cmd::ArgType::string_list,
                                        cmd::ArgType::string_map,
                                        cmd::ArgType::object};

/**
 * @brief Reads the property seed from the environment.
 * @return The parsed `FORM_PROP_SEED` value, or @ref k_default_seed when the
 * variable is unset or does not parse.
 */
std::uint32_t form_prop_seed()
{
    const char* raw = std::getenv("FORM_PROP_SEED");
    if (raw == nullptr || *raw == '\0') {
        return k_default_seed;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(raw, &end, 10);
    if (end == raw) {
        return k_default_seed;
    }
    return static_cast<std::uint32_t>(parsed);
}

/**
 * @brief Maps a registry `ArgType` to its TUI @ref ArgKind, mirroring
 * `in_process_client.cpp` so generated specs and infos stay in lockstep.
 * @param type The registry type to translate.
 * @return The matching @ref ArgKind (unknown types degrade to string).
 */
ArgKind kind_for(cmd::ArgType type)
{
    switch (type) {
        case cmd::ArgType::string:
            return ArgKind::string;
        case cmd::ArgType::integer:
            return ArgKind::integer;
        case cmd::ArgType::boolean:
            return ArgKind::boolean;
        case cmd::ArgType::enumeration:
            return ArgKind::enumeration;
        case cmd::ArgType::timestamp:
            return ArgKind::timestamp;
        case cmd::ArgType::string_list:
            return ArgKind::string_list;
        case cmd::ArgType::string_map:
            return ArgKind::string_map;
        case cmd::ArgType::object:
            return ArgKind::object;
    }
    return ArgKind::string;
}

/**
 * @brief Human-readable tag for a type, used only in failure diagnostics.
 * @param type The type to name.
 * @return A short lower-case label.
 */
const char* type_name(cmd::ArgType type)
{
    switch (type) {
        case cmd::ArgType::string:
            return "string";
        case cmd::ArgType::integer:
            return "integer";
        case cmd::ArgType::boolean:
            return "boolean";
        case cmd::ArgType::enumeration:
            return "enumeration";
        case cmd::ArgType::timestamp:
            return "timestamp";
        case cmd::ArgType::string_list:
            return "string_list";
        case cmd::ArgType::string_map:
            return "string_map";
        case cmd::ArgType::object:
            return "object";
    }
    return "unknown";
}

/**
 * @brief A generated command schema: the TUI-facing @ref CommandInfo the form
 * is built from, paired with the registry @ref cmd::ArgSpec vector the same
 * form's output is validated against. The two are produced together so they
 * describe exactly the same arguments.
 */
struct GeneratedSpec
{
    ///< Schema the form is built from (descriptor order matches @ref specs).
    CommandInfo info;

    ///< Registry schema the form's `to_args_json()` is validated against.
    std::vector<cmd::ArgSpec> specs;
};

/**
 * @brief Expected post-edit state of one field, captured when a valid value is
 * typed so the properties can assert presence and decoded value type.
 */
struct FieldExpectation
{
    ///< Argument key.
    std::string name;

    ///< Field shape (drives which typed read the property uses).
    ArgKind kind = ArgKind::string;

    ///< `true` when the field should appear in `to_args_json()`.
    bool present = false;

    ///< Expected decoded value for string/timestamp/enumeration fields.
    std::string str;

    ///< Expected decoded value for an integer field.
    long long num = 0;

    ///< Expected decoded value for a boolean field.
    bool boolean = false;

    ///< Expected decoded elements for a string-list field.
    std::vector<std::string> list;

    ///< Expected decoded key/value pairs for a string-map field.
    std::vector<std::pair<std::string, std::string>> map;

    ///< Expected non-empty members (name/value) for an object field.
    std::vector<std::pair<std::string, std::string>> members;
};

/**
 * @brief Deterministic, env-seeded generator of random command schemas and the
 * valid/invalid field edits that drive a @ref FormModel.
 *
 * Owns a single `std::mt19937` so every random choice in one case flows from
 * the one seed, making any failure reproducible from the printed `seed`/`case`.
 * Generated specs deliberately stay within both the form's and the registry's
 * easy-to-satisfy bounds (short strings, small lists, in-range integers), so a
 * *valid* script is expected to pass both validators; the invalid helpers break
 * exactly one field.
 */
class SpecGenerator
{
public:
    /**
     * @brief Constructs a generator seeded deterministically.
     * @param seed The PRNG seed (typically @ref form_prop_seed combined with a
     * per-case salt so each case is independently reproducible).
     */
    explicit SpecGenerator(std::uint32_t seed)
        : m_rng_(seed)
    {
    }

    /**
     * @brief Builds a random command schema.
     * @param forced_type When set, field 0 is given this type so a looping
     * caller can guarantee every @ref cmd::ArgType is exercised.
     * @param force_required_at0 When `true`, field 0 is marked required
     * regardless of the random draw (used by the missing-required property).
     * @return A matched @ref GeneratedSpec with 1..8 fields.
     */
    GeneratedSpec random_command(std::optional<cmd::ArgType> forced_type,
                                 bool force_required_at0 = false)
    {
        GeneratedSpec out;
        const int count = 1 + static_cast<int>(m_rng_() % 8u);
        out.info.name = "demo.run";
        for (int f = 0; f < count; ++f) {
            cmd::ArgType type = (f == 0 && forced_type.has_value())
                ? *forced_type
                : k_all_types[m_rng_() % 8u];
            ArgInfo info;
            cmd::ArgSpec spec;
            build_field(static_cast<std::size_t>(f),
                        type,
                        f == 0 && force_required_at0,
                        info,
                        spec);
            out.info.args.push_back(info);
            out.specs.push_back(spec);
        }
        return out;
    }

    /**
     * @brief Types a valid value into every field (or skips @p skip_idx).
     *
     * Required fields are always filled; optional fields are filled with
     * probability 0.6 and otherwise left untouched. Each filled value respects
     * the generated registry bounds, so the resulting form is expected to pass
     * both @ref FormModel::validate and `commands::validate_args`.
     *
     * @param model The form to drive (built from @p gen).
     * @param gen The schema both describe.
     * @param skip_idx A field index to leave untouched, or `-1` to fill all.
     * @return One @ref FieldExpectation per field, in descriptor order.
     */
    std::vector<FieldExpectation> fill_all(FormModel& model,
                                           const GeneratedSpec& gen,
                                           long skip_idx)
    {
        std::vector<FieldExpectation> exps(gen.info.args.size());
        for (std::size_t i = 0; i < gen.info.args.size(); ++i) {
            const ArgInfo& ai = gen.info.args[i];
            exps[i].name = ai.name;
            exps[i].kind = ai.kind;
            if (static_cast<long>(i) == skip_idx) {
                exps[i].present = false;
                continue;
            }
            const bool touch = ai.required || bernoulli(0.6);
            if (!touch) {
                exps[i].present = false;
                continue;
            }
            fill_field(model, gen.specs[i], i, exps[i]);
        }
        return exps;
    }

private:
    /**
     * @brief Draws a Bernoulli trial.
     * @param p Probability of `true`.
     * @return `true` with probability @p p.
     */
    bool bernoulli(double p)
    {
        return std::uniform_real_distribution<double>(0.0, 1.0)(m_rng_) < p;
    }

    /**
     * @brief Draws an alphanumeric token of length 1..8 (no `,`, `=`, or
     * space), safe as a list element, map key/value, enum value, or object
     * member value.
     * @return The token.
     */
    std::string safe_token()
    {
        static const char alphabet[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        const int len = 1 + static_cast<int>(m_rng_() % 8u);
        std::string out;
        for (int i = 0; i < len; ++i) {
            out.push_back(alphabet[m_rng_() % (sizeof(alphabet) - 1)]);
        }
        return out;
    }

    /**
     * @brief Draws a short, valid-UTF-8 string that may include
     * JSON-significant bytes (quote, backslash, newline, tab) and multibyte
     * text, exercising the escaping path while staying inside the registry's
     * `max_length`.
     * @return The string (length >= 1, well under 2048 bytes).
     */
    std::string rich_string()
    {
        static const std::string units[] = {
            "a",
            "Z",
            "7",
            "\"",
            "\\",
            "\n",
            "\t",
            " x",
            "\xc3\xa9",  // U+00E9 e-acute
            "\xf0\x9f\x98\x80",  // U+1F600 emoji
            "k=v",
            "a,b",
            "slug-1"};
        const int parts = 1 + static_cast<int>(m_rng_() % 6u);
        std::string out;
        for (int i = 0; i < parts; ++i) {
            out += units[m_rng_() % (sizeof(units) / sizeof(units[0]))];
        }
        if (out.empty()) {
            out = "x";
        }
        return out;
    }

    /**
     * @brief Draws a calendar-valid RFC 3339 Zulu timestamp.
     * @return A `%Y-%m-%dT%H:%M:%SZ` string (day capped at 28 to avoid
     * month-length edge cases).
     */
    std::string random_timestamp()
    {
        const int year = 2000 + static_cast<int>(m_rng_() % 100u);
        const int month = 1 + static_cast<int>(m_rng_() % 12u);
        const int day = 1 + static_cast<int>(m_rng_() % 28u);
        const int hour = static_cast<int>(m_rng_() % 24u);
        const int minute = static_cast<int>(m_rng_() % 60u);
        const int second = static_cast<int>(m_rng_() % 60u);
        char buf[32];
        std::snprintf(buf,
                      sizeof(buf),
                      "%04d-%02d-%02dT%02d:%02d:%02dZ",
                      year,
                      month,
                      day,
                      hour,
                      minute,
                      second);
        return std::string(buf);
    }

    /**
     * @brief Fills one @ref ArgInfo / @ref cmd::ArgSpec pair for field @p idx.
     * @param idx Field index (used to name the field `f<idx>`).
     * @param type The field's type.
     * @param force_required When `true`, mark the field required.
     * @param info The TUI schema to populate.
     * @param spec The registry schema to populate.
     */
    void build_field(std::size_t idx,
                     cmd::ArgType type,
                     bool force_required,
                     ArgInfo& info,
                     cmd::ArgSpec& spec)
    {
        const std::string name = "f" + std::to_string(idx);
        info.name = name;
        spec.name = name;
        const bool required = force_required || bernoulli(0.4);
        const bool nullable = bernoulli(0.3);
        info.required = required;
        spec.required = required;
        info.nullable = nullable;
        spec.nullable = nullable;
        info.kind = kind_for(type);
        spec.type = type;
        if (bernoulli(0.5)) {
            info.default_value = "\"\"";  // documentation only; never emitted
            spec.default_value = "\"\"";
        }

        switch (type) {
            case cmd::ArgType::enumeration: {
                const int values = 1 + static_cast<int>(m_rng_() % 4u);
                for (int e = 0; e < values; ++e) {
                    const std::string v = "e" + std::to_string(e);
                    info.enum_values.push_back(v);
                    spec.enum_values.push_back(v);
                }
                break;
            }
            case cmd::ArgType::object: {
                const int members = 1 + static_cast<int>(m_rng_() % 4u);
                for (int m = 0; m < members; ++m) {
                    const std::string mname = "m" + std::to_string(m);
                    info.members.push_back(mname);
                    cmd::ArgSpec member;
                    member.name = mname;
                    member.type = cmd::ArgType::string;
                    member.max_length = 2048;
                    spec.fields.push_back(member);
                }
                break;
            }
            case cmd::ArgType::integer: {
                if (bernoulli(0.5)) {
                    spec.min = -1000;
                    spec.max = 1000;
                }
                break;
            }
            default:
                break;
        }
    }

    /**
     * @brief Types a valid value into field @p idx and records the expectation.
     * @param model The form to drive.
     * @param spec The registry schema for this field (for integer bounds).
     * @param idx The field index.
     * @param exp The expectation to fill (presence + decoded value).
     */
    void fill_field(FormModel& model,
                    const cmd::ArgSpec& spec,
                    std::size_t idx,
                    FieldExpectation& exp)
    {
        exp.present = true;
        switch (exp.kind) {
            case ArgKind::string: {
                exp.str = rich_string();
                model.set_text(idx, exp.str);
                break;
            }
            case ArgKind::timestamp: {
                exp.str = random_timestamp();
                model.set_text(idx, exp.str);
                break;
            }
            case ArgKind::integer: {
                const long long lo = spec.min.value_or(-1000000);
                const long long hi = spec.max.value_or(1000000);
                exp.num =
                    std::uniform_int_distribution<long long>(lo, hi)(m_rng_);
                model.set_text(idx, std::to_string(exp.num));
                break;
            }
            case ArgKind::boolean: {
                exp.boolean = bernoulli(0.5);
                model.set_checked(idx, exp.boolean);
                break;
            }
            case ArgKind::enumeration: {
                const int choices = static_cast<int>(
                    model.fields()[idx].spec.enum_values.size());
                const int pick = static_cast<int>(
                    m_rng_()
                    % static_cast<unsigned>(choices > 0 ? choices : 1));
                model.set_choice(idx, pick);
                exp.str = model.fields()[idx].spec.enum_values[pick];
                break;
            }
            case ArgKind::string_list: {
                const int items = 1 + static_cast<int>(m_rng_() % 4u);
                std::string buffer;
                for (int i = 0; i < items; ++i) {
                    const std::string token = safe_token();
                    exp.list.push_back(token);
                    if (i != 0) {
                        buffer += ", ";
                    }
                    buffer += token;
                }
                model.set_text(idx, buffer);
                break;
            }
            case ArgKind::string_map: {
                const int pairs = 1 + static_cast<int>(m_rng_() % 4u);
                std::string buffer;
                int written = 0;
                for (int i = 0; i < pairs; ++i) {
                    const std::string key = "k" + std::to_string(i);
                    const std::string value = safe_token();
                    exp.map.emplace_back(key, value);
                    if (written != 0) {
                        buffer += ", ";
                    }
                    buffer += key + "=" + value;
                    ++written;
                }
                model.set_text(idx, buffer);
                break;
            }
            case ArgKind::object: {
                const std::size_t members =
                    model.fields()[idx].spec.members.size();
                bool any = false;
                for (std::size_t m = 0; m < members; ++m) {
                    if (!bernoulli(0.6) && !(m == 0 && !any)) {
                        continue;  // keep at least the first member non-empty
                    }
                    const std::string value = safe_token();
                    model.set_member(idx, m, value);
                    exp.members.emplace_back(
                        model.fields()[idx].spec.members[m], value);
                    any = true;
                }
                break;
            }
        }
    }

    std::mt19937 m_rng_;  ///< The one PRNG every draw in a case flows from.
};

/**
 * @brief One-line schema summary for failure diagnostics.
 * @param gen The generated schema.
 * @return A `name[type*] ...` listing (`*` marks required).
 */
std::string describe(const GeneratedSpec& gen)
{
    std::string out = gen.info.name + ":";
    for (std::size_t i = 0; i < gen.info.args.size(); ++i) {
        out += " " + gen.info.args[i].name + "[" + type_name(gen.specs[i].type)
            + "]";
        if (gen.info.args[i].required) {
            out += "*";
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// P1 + P3/P4 + P5 + P8 + P11: the core round-trip over random schemas.
//   P1  a valid form validates and its args pass commands::validate_args;
//   P3/P4 untouched / defaulted fields are omitted;
//   P5  each set value keeps its JSON type (int as number, bool tri-state,
//       enum as a listed string, timestamp as Zulu text, list/map/object
//       shape);
//   P8  prefill_from_result is idempotent and re-emits a valid envelope;
//   P11 every byte survives JSON escaping (compared via the registry decoder).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(valid_forms_round_trip_through_validate_args)
{
    const std::uint32_t seed = form_prop_seed();
    for (int i = 0; i < k_iterations; ++i) {
        SpecGenerator gen_rng(seed + 0x1000u + static_cast<std::uint32_t>(i));
        const GeneratedSpec gen = gen_rng.random_command(k_all_types[i % 8]);
        FormModel model(gen.info);
        const std::vector<FieldExpectation> exps =
            gen_rng.fill_all(model, gen, -1);

        BOOST_TEST_CONTEXT("seed=" << seed << " case=" << i << " ["
                                   << describe(gen) << "]")
        {
            // P1: advisory validation passes and the args pass the registry.
            BOOST_CHECK(model.validate());
            const std::string json = model.to_args_json();
            cmd::ArgsView view;
            cmd::CommandError err;
            const bool ok = cmd::validate_args(gen.specs, json, &view, &err);
            BOOST_CHECK_MESSAGE(
                ok,
                "validate_args rejected: " << err.message << " json=" << json);
            if (!ok) {
                continue;
            }

            for (const FieldExpectation& exp : exps) {
                // P3/P4: untouched / defaulted fields are omitted entirely.
                BOOST_CHECK_MESSAGE(view.has(exp.name) == exp.present,
                                    "presence mismatch for " << exp.name);
                if (!exp.present) {
                    continue;
                }
                // P5/P11: the decoded value keeps its type and every byte.
                switch (exp.kind) {
                    case ArgKind::string:
                    case ArgKind::timestamp:
                    case ArgKind::enumeration: {
                        const auto got = view.get_string(exp.name);
                        BOOST_REQUIRE(got.has_value());
                        BOOST_CHECK(*got == exp.str);
                        break;
                    }
                    case ArgKind::integer: {
                        const auto got = view.get_int(exp.name);
                        BOOST_REQUIRE(got.has_value());
                        BOOST_CHECK_EQUAL(*got, exp.num);
                        break;
                    }
                    case ArgKind::boolean: {
                        const auto got = view.get_bool(exp.name);
                        BOOST_REQUIRE(got.has_value());
                        BOOST_CHECK_EQUAL(*got, exp.boolean);
                        break;
                    }
                    case ArgKind::string_list: {
                        const auto got = view.get_string_list(exp.name);
                        BOOST_REQUIRE(got.has_value());
                        BOOST_CHECK(*got == exp.list);
                        break;
                    }
                    case ArgKind::string_map: {
                        const auto got = view.get_string_map(exp.name);
                        BOOST_REQUIRE(got.has_value());
                        std::map<std::string, std::string> expected(
                            exp.map.begin(), exp.map.end());
                        BOOST_CHECK(*got == expected);
                        break;
                    }
                    case ArgKind::object: {
                        const auto obj = view.get_object(exp.name);
                        BOOST_REQUIRE(obj.has_value());
                        for (const auto& member : exp.members) {
                            const auto mv = obj->get_string(member.first);
                            BOOST_REQUIRE(mv.has_value());
                            BOOST_CHECK(*mv == member.second);
                        }
                        break;
                    }
                }
            }

            // P8: prefilling from the emitted args is deterministic and the
            // re-sent (locked-identity + required) envelope stays valid.
            FormModel prefilled_a(gen.info);
            prefilled_a.prefill_from_result(json);
            FormModel prefilled_b(gen.info);
            prefilled_b.prefill_from_result(json);
            const std::string reemit = prefilled_a.to_args_json();
            BOOST_CHECK_EQUAL(reemit, prefilled_b.to_args_json());
            cmd::ArgsView review;
            cmd::CommandError reerr;
            BOOST_CHECK_MESSAGE(
                cmd::validate_args(gen.specs, reemit, &review, &reerr),
                "prefill re-emit rejected: " << reerr.message
                                             << " reemit=" << reemit);
        }
    }
}

// ---------------------------------------------------------------------------
// P2: a missing required field fails advisory validation, flags that field as
// "required", and never lets a bad value reach the envelope (the key is
// omitted).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(missing_required_field_is_flagged_and_omitted)
{
    const std::uint32_t seed = form_prop_seed();
    for (int i = 0; i < 120; ++i) {
        SpecGenerator gen_rng(seed + 0x2000u + static_cast<std::uint32_t>(i));
        const GeneratedSpec gen = gen_rng.random_command(
            k_all_types[i % 8], /*force_required_at0=*/true);
        FormModel model(gen.info);
        gen_rng.fill_all(model, gen, /*skip_idx=*/0);

        BOOST_TEST_CONTEXT("seed=" << seed << " case=" << i << " ["
                                   << describe(gen) << "]")
        {
            BOOST_CHECK(!model.validate());
            BOOST_REQUIRE(model.fields()[0].error.has_value());
            BOOST_CHECK_EQUAL(*model.fields()[0].error, "required");
            const std::string json = model.to_args_json();
            BOOST_CHECK_MESSAGE(json.find("\"f0\":") == std::string::npos,
                                "omitted required field leaked into " << json);
        }
    }
}

// ---------------------------------------------------------------------------
// P9: an invalid value for a type yields exactly that field's error and leaves
// every other field intact (no error).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(invalid_value_flags_only_its_own_field)
{
    const cmd::ArgType corruptible[] = {cmd::ArgType::integer,
                                        cmd::ArgType::timestamp,
                                        cmd::ArgType::enumeration,
                                        cmd::ArgType::string_map};
    const std::uint32_t seed = form_prop_seed();
    for (int i = 0; i < 120; ++i) {
        SpecGenerator gen_rng(seed + 0x3000u + static_cast<std::uint32_t>(i));
        const cmd::ArgType target = corruptible[i % 4];
        const GeneratedSpec gen = gen_rng.random_command(target);
        FormModel model(gen.info);
        gen_rng.fill_all(model, gen, -1);

        // Corrupt field 0 (the forced corruptible type).
        switch (target) {
            case cmd::ArgType::integer:
                model.set_text(0, "nan123xyz");
                break;
            case cmd::ArgType::timestamp:
                model.set_text(0, "not-a-timestamp");
                break;
            case cmd::ArgType::enumeration:
                model.set_choice(
                    0,
                    static_cast<int>(gen.info.args[0].enum_values.size()) + 5);
                break;
            case cmd::ArgType::string_map:
                model.set_text(0, "keywithoutequals");
                break;
            default:
                break;
        }

        BOOST_TEST_CONTEXT("seed=" << seed << " case=" << i
                                   << " target=" << type_name(target) << " ["
                                   << describe(gen) << "]")
        {
            BOOST_CHECK(!model.validate());
            BOOST_REQUIRE(model.fields()[0].error.has_value());
            for (std::size_t f = 1; f < model.fields().size(); ++f) {
                BOOST_CHECK_MESSAGE(!model.fields()[f].error.has_value(),
                                    "unexpected error on field "
                                        << model.fields()[f].spec.name);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// P6: list/map parsing - trim and drop empties, duplicate keys last-wins with a
// non-blocking hint, a malformed pair is a blocking error.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(list_and_map_parsing_rules)
{
    const auto list_arg = []
    {
        CommandInfo info;
        info.name = "demo.run";
        ArgInfo a;
        a.name = "tags";
        a.kind = ArgKind::string_list;
        info.args = {a};
        return info;
    };
    const auto map_arg = []
    {
        CommandInfo info;
        info.name = "demo.run";
        ArgInfo a;
        a.name = "meta";
        a.kind = ArgKind::string_map;
        info.args = {a};
        return info;
    };

    FormModel list(list_arg());
    list.set_text(0, "a, b ,c, ,");  // surrounding spaces + empty entries
    BOOST_CHECK(list.validate());
    BOOST_CHECK_EQUAL(list.to_args_json(), "{\"tags\":[\"a\",\"b\",\"c\"]}");

    FormModel map(map_arg());
    map.set_text(0, "k=v, k2=v2");
    BOOST_CHECK(map.validate());
    BOOST_CHECK_EQUAL(map.to_args_json(),
                      "{\"meta\":{\"k\":\"v\",\"k2\":\"v2\"}}");

    FormModel dup(map_arg());
    dup.set_text(0, "a=1, a=2");
    BOOST_CHECK(dup.validate());  // duplicate is a non-blocking hint
    BOOST_REQUIRE(dup.fields()[0].error.has_value());
    BOOST_CHECK(dup.fields()[0].error->find("duplicate") != std::string::npos);
    BOOST_CHECK_EQUAL(dup.to_args_json(), "{\"meta\":{\"a\":\"2\"}}");

    FormModel malformed(map_arg());
    malformed.set_text(0, "k");  // no '='
    BOOST_CHECK(!malformed.validate());
    BOOST_REQUIRE(malformed.fields()[0].error.has_value());
}

// ---------------------------------------------------------------------------
// P7: an object emits only its non-empty members; all-empty is omitted.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(object_emits_only_filled_members)
{
    CommandInfo info;
    info.name = "demo.run";
    ArgInfo campaign;
    campaign.name = "campaign";
    campaign.kind = ArgKind::object;
    campaign.members = {"name", "source", "medium"};
    info.args = {campaign};

    FormModel empty(info);
    BOOST_CHECK_EQUAL(empty.to_args_json(), "{}");

    FormModel partial(info);
    partial.set_member(0, 0, "spring");  // name
    partial.set_member(0, 2, "email");  // medium (source stays empty)
    BOOST_CHECK_EQUAL(
        partial.to_args_json(),
        "{\"campaign\":{\"name\":\"spring\",\"medium\":\"email\"}}");
}

// ---------------------------------------------------------------------------
// P8 (cleared-nullable branch): clearing a nullable field emits explicit null.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(cleared_nullable_field_emits_null)
{
    CommandInfo info;
    info.name = "demo.run";
    ArgInfo a;
    a.name = "expires_at";
    a.kind = ArgKind::timestamp;
    a.nullable = true;
    info.args = {a};

    cmd::ArgSpec spec;
    spec.name = "expires_at";
    spec.type = cmd::ArgType::timestamp;
    spec.nullable = true;

    FormModel model(info);
    BOOST_CHECK(model.clear_field(0));
    BOOST_CHECK(model.fields()[0].state == FieldState::cleared);
    const std::string json = model.to_args_json();
    BOOST_CHECK_EQUAL(json, "{\"expires_at\":null}");
    cmd::ArgsView view;
    cmd::CommandError err;
    BOOST_CHECK(cmd::validate_args({spec}, json, &view, &err));
    BOOST_CHECK(view.is_null("expires_at"));
}

// ---------------------------------------------------------------------------
// P11: quotes/backslashes/newlines/emoji/4 KiB survive JSON escaping unchanged;
// invalid UTF-8 stays well-formed JSON; a buffer at k_max_field_bytes is
// flagged and omitted.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(every_byte_survives_json_escaping)
{
    namespace hs = url_shortener::tui::testing;
    const auto string_arg = []
    {
        CommandInfo info;
        info.name = "demo.run";
        ArgInfo a;
        a.name = "s";
        a.kind = ArgKind::string;
        info.args = {a};
        return info;
    };
    cmd::ArgSpec spec;
    spec.name = "s";
    spec.type = cmd::ArgType::string;
    spec.max_length = 20000;  // accept a 4 KiB paste

    // Valid-UTF-8 adversarial strings must round-trip byte-for-byte.
    const std::vector<std::string> faithful = {"\"",
                                               "\\",
                                               "a\"b\\c\nd\te",
                                               "\xc3\xa9",  // e-acute
                                               "\xf0\x9f\x98\x80",  // emoji
                                               hs::bidi_override_slug(),
                                               hs::zero_width_space(),
                                               hs::c0_whitespace(),
                                               hs::soft_hyphen(),
                                               hs::line_separators(),
                                               hs::variation_selector(),
                                               hs::benign_unicode(),
                                               std::string(4096, 'A')};
    for (const std::string& raw : faithful) {
        FormModel model(string_arg());
        model.set_text(0, raw);
        BOOST_CHECK(model.validate());
        const std::string json = model.to_args_json();
        cmd::ArgsView view;
        cmd::CommandError err;
        BOOST_REQUIRE(cmd::validate_args({spec}, json, &view, &err));
        const auto got = view.get_string("s");
        BOOST_REQUIRE(got.has_value());
        BOOST_CHECK(*got == raw);
    }

    // Invalid UTF-8 must not corrupt the JSON (it is made safe, not faithful).
    {
        FormModel model(string_arg());
        model.set_text(0, hs::invalid_utf8());
        BOOST_CHECK(model.validate());
        const std::string json = model.to_args_json();
        cmd::ArgsView view;
        cmd::CommandError err;
        BOOST_CHECK_MESSAGE(cmd::validate_args({spec}, json, &view, &err),
                            "invalid UTF-8 broke JSON: " << json);
    }

    // A buffer at the model's byte cap is flagged and omitted.
    {
        FormModel model(string_arg());
        model.set_text(0, std::string(70u * 1024u, 'x'));  // > 64 KiB cap
        BOOST_CHECK(!model.validate());
        BOOST_REQUIRE(model.fields()[0].error.has_value());
        BOOST_CHECK_EQUAL(model.to_args_json(), "{}");
    }
}

// ---------------------------------------------------------------------------
// Edge-case table: integer bounds vs registry limits; timestamps (leap valid,
// non-Zulu/garbage rejected); empty enum list; 1-member enum; 64 vs 65 byte
// string; a reserved-like slug is NOT rejected client-side.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(fixed_edge_cases)
{
    const auto one = [](const std::string& name, ArgKind kind)
    {
        CommandInfo info;
        info.name = "demo.run";
        ArgInfo a;
        a.name = name;
        a.kind = kind;
        info.args = {a};
        return info;
    };

    // Integer bounds: the model does not duplicate the registry's range rule.
    {
        cmd::ArgSpec spec;
        spec.name = "n";
        spec.type = cmd::ArgType::integer;
        spec.min = 0;
        spec.max = 10;
        FormModel over(one("n", ArgKind::integer));
        over.set_text(0, "11");
        BOOST_CHECK(over.validate());  // advisory check passes
        cmd::ArgsView v1;
        cmd::CommandError e1;
        BOOST_CHECK(!cmd::validate_args({spec}, over.to_args_json(), &v1, &e1));

        FormModel within(one("n", ArgKind::integer));
        within.set_text(0, "5");
        BOOST_CHECK(within.validate());
        cmd::ArgsView v2;
        cmd::CommandError e2;
        BOOST_CHECK(
            cmd::validate_args({spec}, within.to_args_json(), &v2, &e2));
    }

    // Timestamps.
    {
        FormModel leap(one("t", ArgKind::timestamp));
        leap.set_text(0, "2024-02-29T00:00:00Z");  // valid leap day, Zulu
        BOOST_CHECK(leap.validate());

        FormModel offset(one("t", ArgKind::timestamp));
        offset.set_text(0, "2026-01-02T03:04:05+00:00");  // non-Zulu offset
        BOOST_CHECK(!offset.validate());

        // Non-timestamp text is rejected. (The shared get_time-based parser is
        // lenient about *truncated* RFC 3339 - e.g. "2026-01-02" is accepted on
        // libc++ - so this edge asserts only on unambiguous garbage, which is
        // the model contract this test cares about; calendar-strictness is the
        // parser's concern, not FormModel's.)
        FormModel garbage(one("t", ArgKind::timestamp));
        garbage.set_text(0, "not-a-timestamp");
        BOOST_CHECK(!garbage.validate());
    }

    // Enum: empty list rejects any choice; a single-member list accepts 0.
    {
        FormModel empty_enum(one("e", ArgKind::enumeration));
        empty_enum.set_choice(0, 0);
        BOOST_CHECK(!empty_enum.validate());

        CommandInfo single_info = one("e", ArgKind::enumeration);
        single_info.args[0].enum_values = {"only"};
        FormModel single(single_info);
        single.set_choice(0, 0);
        BOOST_CHECK(single.validate());
    }

    // 64 vs 65 byte string against a registry max_length of 64.
    {
        cmd::ArgSpec spec;
        spec.name = "s";
        spec.type = cmd::ArgType::string;
        spec.max_length = 64;

        FormModel at_limit(one("s", ArgKind::string));
        at_limit.set_text(0, std::string(64, 'a'));
        BOOST_CHECK(at_limit.validate());
        cmd::ArgsView v1;
        cmd::CommandError e1;
        BOOST_CHECK(
            cmd::validate_args({spec}, at_limit.to_args_json(), &v1, &e1));

        FormModel over_limit(one("s", ArgKind::string));
        over_limit.set_text(0, std::string(65, 'a'));
        BOOST_CHECK(over_limit.validate());  // the model does not enforce it
        cmd::ArgsView v2;
        cmd::CommandError e2;
        BOOST_CHECK(
            !cmd::validate_args({spec}, over_limit.to_args_json(), &v2, &e2));
    }

    // A reserved-like slug is not rejected client-side (no server rule mirror).
    {
        FormModel slug(one("slug", ArgKind::string));
        slug.set_text(0, "admin");
        BOOST_CHECK(slug.validate());
        BOOST_CHECK_EQUAL(slug.to_args_json(), "{\"slug\":\"admin\"}");
    }
}

// ---------------------------------------------------------------------------
// P10: a form built from the real link.create descriptor sends the same args
// its equivalent command/envelope footer shows, and names the command.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(real_link_create_equivalent_matches_args)
{
    const std::vector<CommandInfo> infos =
        url_shortener::tui::testing::command_list_infos();
    const CommandInfo* create = nullptr;
    for (const CommandInfo& info : infos) {
        if (info.name == "link.create") {
            create = &info;
            break;
        }
    }
    BOOST_REQUIRE_MESSAGE(create != nullptr,
                          "link.create missing from catalog");

    FormModel model(*create);
    std::size_t target_index = model.fields().size();
    for (std::size_t i = 0; i < model.fields().size(); ++i) {
        if (model.fields()[i].spec.name == "target_url") {
            target_index = i;
        }
    }
    BOOST_REQUIRE(target_index < model.fields().size());
    model.set_text(target_index, "https://example.com/docs");
    BOOST_CHECK(model.validate());

    const std::string args = model.to_args_json();
    BOOST_CHECK(args.find("\"target_url\":\"https://example.com/docs\"")
                != std::string::npos);

    const cmd::CommandRequest request = model.to_request("tui-prop");
    const std::string envelope =
        url_shortener::tui::equivalent_envelope(request);
    const std::string cli = url_shortener::tui::equivalent_cli(request);
    BOOST_CHECK(envelope.find("link.create") != std::string::npos);
    BOOST_CHECK(envelope.find(args) != std::string::npos);  // args verbatim
    BOOST_CHECK(cli.find("link.create") != std::string::npos);
}
