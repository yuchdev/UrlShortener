#pragma once

/**
 * @file mini_json.hpp
 * @brief Test-only JSON value, parser and serializer for the command corpus.
 *
 * This is a deliberately independent, ~RFC 8259 subset DOM used **only** by the
 * conformance support library (decision log D3). It lives under `tests/`, is
 * linked only into `conformance_support`, and never ships in product code - the
 * product has exactly one JSON tokenizer (`commands::scan_object`,
 * `include/url_shortener/commands/json_scan.hpp`). Keeping the conformance DOM
 * separate means a scanner bug cannot hide itself inside the conformance
 * verdict. 07.0/21's structural test forbids `src/` or `include/` from including
 * this header.
 *
 * The parser is depth-bounded (default 64) and never throws or crashes on
 * malformed or hostile input: it reports a reason through an out-parameter and
 * returns `std::nullopt`. Objects preserve insertion order and reject duplicate
 * keys; trailing commas and trailing data are rejected.
 */

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace conformance
{

/**
 * @brief A tagged JSON value with insertion-ordered object members.
 *
 * `Int` and `Double` are kept distinct (no float is used for an integral
 * literal) so the matcher can honour rule N3 (a boolean is never a number, an
 * integral double such as `3.0` still counts as an integer). All accessors are
 * `const`; mutation happens only through the explicit builder helpers used by
 * the parser and the case runner.
 */
class Json
{
public:
    /// The JSON value kinds this DOM distinguishes.
    enum class Kind
    {
        Null,    ///< The JSON `null` literal.
        Bool,    ///< `true` or `false`.
        Int,     ///< An integral JSON number (no `.`/`e`/`E` in the literal).
        Double,  ///< A fractional or exponential JSON number.
        String,  ///< A JSON string (decoded to UTF-8).
        Array,   ///< A JSON array.
        Object   ///< A JSON object (members kept in insertion order).
    };

    /// One object member: a decoded key and its value, in insertion order.
    using Member = std::pair<std::string, Json>;

    /// Constructs a `Null` value.
    Json() = default;

    /// @return A `Null` value.
    static Json null();
    /// @param value The boolean payload. @return A `Bool` value.
    static Json boolean(bool value);
    /// @param value The integral payload. @return an `Int` value.
    static Json integer(long long value);
    /// @param value The floating payload. @return A `Double` value.
    static Json real(double value);
    /// @param value The UTF-8 payload (moved). @return A `String` value.
    static Json string(std::string value);
    /// @return An empty `Array` value.
    static Json array();
    /// @return An empty `Object` value.
    static Json object();

    /**
     * @brief Parses exactly one JSON document from @p text.
     *
     * Leading/trailing JSON whitespace is allowed; any non-whitespace after the
     * top-level value is `trailing_data`. Never throws.
     *
     * @param text Candidate JSON (UTF-8); treated as untrusted and never logged.
     * @param error Optional out-parameter; when parsing fails it receives a
     * short reason (e.g. `"malformed number"`, `"duplicate key"`,
     * `"max depth exceeded"`, `"trailing data"`). Untouched on success.
     * @param max_depth Maximum nesting depth of arrays/objects; a deeper
     * document fails cleanly with `error == "max depth exceeded"` rather than
     * recursing without bound. Defaults to 64.
     * @return The parsed value on success, or `std::nullopt` on any failure.
     */
    static std::optional<Json> parse(std::string_view text,
                                     std::string* error,
                                     int max_depth = 64);

    /**
     * @brief Serializes this value to compact JSON.
     *
     * Object keys are emitted in insertion order (stable). A `Double` that has
     * no fractional component is emitted with a trailing `.0` so it round-trips
     * back to `Kind::Double`. Strings are escaped minimally; non-ASCII bytes are
     * passed through as UTF-8.
     *
     * @return The compact JSON text.
     */
    std::string dump() const;

    /// @return The kind tag of this value.
    Kind kind() const { return m_kind; }

    /// @return `true` iff this value is a `Null`.
    bool is_null() const { return m_kind == Kind::Null; }
    /// @return `true` iff this value is a `Bool`.
    bool is_bool() const { return m_kind == Kind::Bool; }
    /// @return `true` iff this value is a `String`.
    bool is_string() const { return m_kind == Kind::String; }
    /// @return `true` iff this value is an `Array`.
    bool is_array() const { return m_kind == Kind::Array; }
    /// @return `true` iff this value is an `Object`.
    bool is_object() const { return m_kind == Kind::Object; }
    /// @return `true` iff this value is an `Int` or a `Double`.
    bool is_number() const
    {
        return m_kind == Kind::Int || m_kind == Kind::Double;
    }

    /**
     * @brief Looks up an object member by key.
     * @param key The member key to find.
     * @return A pointer to the member value, or `nullptr` if this value is not
     * an object or the key is absent. Valid while this value is unmodified.
     */
    const Json* find(std::string_view key) const;

    /**
     * @brief Indexes an array element.
     * @param index Zero-based element index.
     * @return A pointer to the element, or `nullptr` if this value is not an
     * array or @p index is out of range.
     */
    const Json* at(std::size_t index) const;

    /**
     * @brief Resolves a restricted JSONPath of the form `$.a.b[0].c`.
     *
     * Only dotted object keys and `[index]` subscripts are supported; wildcards,
     * recursive descent (`..`) and filters are rejected (return `nullptr`),
     * matching the Python `jsonpath_get` accessor used by `save`.
     *
     * @param jsonpath The path expression; must begin with `$`.
     * @return A pointer to the located value, or `nullptr` if the path is
     * malformed or does not resolve.
     */
    const Json* path(std::string_view jsonpath) const;

    /// @return The UTF-8 payload. Programmer error (asserts) unless `String`.
    const std::string& as_string() const;
    /// @return The integral payload. Programmer error (asserts) unless `Int`.
    long long as_int() const;
    /// @return The floating payload. Programmer error (asserts) unless `Double`.
    double as_double() const;
    /// @return The boolean payload. Programmer error (asserts) unless `Bool`.
    bool as_bool() const;

    /// @return The array elements. Programmer error (asserts) unless `Array`.
    const std::vector<Json>& items() const;
    /// @return The object members. Programmer error (asserts) unless `Object`.
    const std::vector<Member>& members() const;

    /**
     * @brief Appends an element to this array value.
     * @param value The element to append (moved). Precondition: `Array`.
     */
    void push_back(Json value);

    /**
     * @brief Appends a member to this object value without de-duplication.
     * @param key The member key (moved). @param value The member value (moved).
     * Precondition: `Object`. The parser guarantees uniqueness; builder callers
     * that construct expected envelopes must not introduce a duplicate key.
     */
    void set(std::string key, Json value);

private:
    Kind m_kind = Kind::Null;        ///< Active value kind.
    bool m_bool = false;             ///< Payload when `m_kind == Bool`.
    long long m_int = 0;             ///< Payload when `m_kind == Int`.
    double m_double = 0.0;           ///< Payload when `m_kind == Double`.
    std::string m_string;            ///< Payload when `m_kind == String`.
    std::vector<Json> m_array;       ///< Payload when `m_kind == Array`.
    std::vector<Member> m_object;    ///< Payload when `m_kind == Object`.
};

}  // namespace conformance
