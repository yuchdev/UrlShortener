#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/command_client.hpp>

namespace url_shortener::tui
{

/**
 * @file form_model.hpp
 * @brief The FTXUI-free half of the generic form builder (ADR 0003 Decision 2,
 * "generic first"): @ref FormModel turns one @ref CommandInfo into per-field
 * editing state, validates advisory-only on the client, and produces the exact
 * `commands::CommandRequest` the registry expects (Task 06.0 subtask 07).
 *
 * This model is the data layer behind D5 (new/edit link form) and the
 * "GenericForm" node of D9/D11; its @ref FormModel::to_args_json is the only
 * place the TUI constructs an `args` object. It lives in
 * `url_shortener_tui_model` (never links FTXUI) and knows no specific command
 * name - every behavior is driven by the @ref ArgInfo schema, so a new command
 * gets a working form with zero TUI code (D9).
 *
 * Trust and display: the model never sanitizes for a terminal. The JSON it
 * emits escapes every byte correctly through `url_shortener::jsonEscape`
 * (quotes, backslashes, control characters and invalid UTF-8 all become safe
 * `\\u`/`\\uFFFD` forms), so `args_json` is well-formed for the registry. But
 * every *display* string the model exposes - field editing buffers (which hold
 * raw server values after @ref FormModel::prefill_from_result), the registry's
 * labels/help carried in @ref ArgInfo::doc / @ref ArgInfo::enum_values, and the
 * text of @ref FormField::error and @ref FormModel::validation_summary - is
 * handed out raw and MUST be passed through `text_fit::sanitize_for_terminal`
 * by the view (subtask 08) before it reaches a render sink (threat model
 * T-110), exactly as `ArgInfo::doc` and the confirm model already document. The
 * model's own validation messages are fixed ASCII, but because they are
 * concatenated with server-supplied argument names the whole summary line must
 * still be sanitized at the sink.
 */

/**
 * @brief The three editing states of a form field, which decide how
 * @ref FormModel::to_args_json encodes it under PATCH semantics.
 */
enum class FieldState
{
    ///< The operator has not touched the field: its key is omitted from `args`
    ///< entirely, so the handler applies its documented default (create) or
    ///< leaves the value unchanged (update).
    untouched,

    ///< The operator supplied a value: the field's current value is encoded.
    set,

    ///< The operator cleared a nullable field: an explicit JSON `null` is
    ///< emitted (reachable only when @ref ArgInfo::nullable is `true`).
    cleared
};

/**
 * @brief One field's full editing state within a @ref FormModel.
 *
 * A plain value type holding the schema plus whichever buffer matches the
 * field's @ref ArgInfo::kind. Only the buffer relevant to the kind is used
 * (a string uses @ref text, a boolean uses @ref checked, and so on); the rest
 * stay at their defaults.
 */
struct FormField
{
    ///< The argument schema this field edits (name, kind, required, nullable,
    ///< enum values, object members, default, doc). A raw server value; its
    ///< display strings are sanitized by the view, not here (T-110).
    ArgInfo spec;

    ///< Editing buffer for string, integer, timestamp, string-list,
    ///< string-map, and member-less (raw-JSON) object fields.
    std::string text;

    ///< Checkbox state for a boolean field; only meaningful once @ref state is
    ///< @ref FieldState::set (an untouched boolean is omitted, not sent false).
    bool checked = false;

    ///< Selected index into @ref ArgInfo::enum_values for an enumeration field;
    ///< `-1` means no choice has been made (field stays untouched).
    int choice = -1;

    ///< Per-member editing buffers for an object field, parallel to
    ///< @ref ArgInfo::members; empty members are omitted from the emitted
    ///< object and an all-empty object is omitted entirely.
    std::vector<std::string> members_text;

    ///< How the field is encoded: omitted, valued, or explicit null.
    FieldState state = FieldState::untouched;

    ///< `true` when the field identifies the resource being edited and must not
    ///< be changed (set by @ref FormModel::prefill_from_result on the required
    ///< identity field, e.g. the `slug` of a `link.update`); the view renders a
    ///< locked field disabled and the mutators treat it as a no-op.
    bool locked = false;

    ///< Advisory, client-side validation message, or a server `invalid_field`
    ///< message the view assigned; `std::nullopt` when the field is valid. A
    ///< display string; sanitized at the render sink (T-110).
    std::optional<std::string> error;
};

/**
 * @brief Construction-time options for a @ref FormModel.
 */
struct FormOptions
{
    ///< `true` when the form offers a dry-run toggle for a command that
    ///< supports it; the toggle only takes effect for a command whose
    ///< @ref CommandInfo::supports_dry_run is `true`.
    bool include_dry_run_toggle = true;

    ///< Prefix the view uses when it generates a request id for this form's
    ///< submissions (the id itself is passed to @ref FormModel::to_request).
    std::string request_id_prefix = "tui";
};

/**
 * @brief Per-field editing state for one command, with advisory validation and
 * exact `CommandRequest` production (ADR 0003 Decision 2, contract C1/C7).
 *
 * Built from a single @ref CommandInfo, a `FormModel` owns one @ref FormField
 * per argument in descriptor order. It never consults authorization or
 * re-implements the registry's `ArgSpec` validation (that stays authoritative);
 * its @ref validate is cheap, client-side and advisory, and its
 * @ref to_args_json / @ref to_request build the one request the TUI submits.
 * Documented defaults are surfaced to the view through @ref ArgInfo::default_value
 * and are never pre-filled into a buffer, so an untouched default is never sent
 * (the handler applies it).
 */
class FormModel
{
public:
    /**
     * @brief Builds a form for @p command.
     *
     * Creates one @ref FormField per @ref CommandInfo::args entry, in
     * descriptor order, every field @ref FieldState::untouched with an empty
     * buffer (an object field's @ref FormField::members_text is sized to its
     * @ref ArgInfo::members). An unrecognized @ref ArgInfo::kind is handled as a
     * string field so a future argument type never crashes the form (D9).
     *
     * @param command The command whose schema drives the form; copied, so the
     * model stays valid after the client that produced it is gone.
     * @param options Form options; defaults enable the dry-run toggle and use
     * the `"tui"` request-id prefix.
     */
    explicit FormModel(const CommandInfo& command, FormOptions options = {});

    /**
     * @brief The command this form edits.
     * @return A reference to the held @ref CommandInfo (valid for the model's
     * lifetime).
     */
    const CommandInfo& command() const;

    /**
     * @brief The form's fields, in descriptor order, for the view to render and
     * to assign server `invalid_field` messages into @ref FormField::error.
     * @return A mutable reference to the field vector.
     */
    std::vector<FormField>& fields();

    /**
     * @brief Read-only access to the form's fields.
     * @return A const reference to the field vector.
     */
    const std::vector<FormField>& fields() const;

    /**
     * @brief Sets a string/integer/timestamp/list/map/raw-object field's buffer.
     *
     * Marks the field @ref FieldState::set, except that clearing an *optional*
     * field to the empty string returns it to @ref FieldState::untouched (so an
     * emptied optional is omitted, preserving PATCH semantics). A required field
     * emptied to `""` stays @ref FieldState::set so @ref validate can flag it. A
     * locked field (identity) is a no-op.
     *
     * @param i Field index; out-of-range indices are ignored.
     * @param text The new buffer contents.
     */
    void set_text(std::size_t i, std::string text);

    /**
     * @brief Sets a boolean field's checkbox and marks it touched.
     *
     * Marks the field @ref FieldState::set so the value is emitted; an untouched
     * boolean is never sent (it is not emitted as `false`). A locked field is a
     * no-op.
     *
     * @param i Field index; out-of-range indices are ignored.
     * @param v The checkbox value to store.
     */
    void set_checked(std::size_t i, bool v);

    /**
     * @brief Sets an enumeration field's selected index.
     *
     * A non-negative @p index marks the field @ref FieldState::set and selects
     * @ref ArgInfo::enum_values at that index; a negative @p index returns the
     * field to @ref FieldState::untouched. A locked field is a no-op.
     *
     * @param i Field index; out-of-range indices are ignored.
     * @param index The enum choice index, or `-1` to unset.
     */
    void set_choice(std::size_t i, int index);

    /**
     * @brief Sets one member buffer of an object field.
     *
     * Marks the object field @ref FieldState::set when any member is non-empty,
     * otherwise returns it to @ref FieldState::untouched (an all-empty object is
     * omitted). A locked field is a no-op.
     *
     * @param i Field index; out-of-range indices are ignored.
     * @param m Member index within @ref FormField::members_text; out-of-range
     * member indices are ignored.
     * @param text The new member buffer contents.
     */
    void set_member(std::size_t i, std::size_t m, std::string text);

    /**
     * @brief Clears a nullable field, so @ref to_args_json emits explicit
     * `null`.
     *
     * Only valid for a field whose @ref ArgInfo::nullable is `true` and that is
     * not locked; for any other field this is a no-op that reports failure, so
     * the view can keep the clear affordance hidden for non-nullable fields.
     *
     * @param i Field index.
     * @return `true` when the field was set to @ref FieldState::cleared; `false`
     * when the index is out of range, the field is not nullable, or the field is
     * locked.
     */
    bool clear_field(std::size_t i);

    /**
     * @brief Sets the form's dry-run toggle.
     *
     * @param v `true` to request a validate-only run; it only reaches the
     * request when both @ref FormOptions::include_dry_run_toggle and
     * @ref CommandInfo::supports_dry_run are `true`.
     */
    void set_dry_run(bool v);

    /**
     * @brief The current dry-run toggle state.
     * @return The value last passed to @ref set_dry_run (`false` initially).
     */
    bool dry_run() const;

    /**
     * @brief Runs cheap, advisory client-side validation and fills
     * @ref FormField::error.
     *
     * Checks only what is cheap and type-local: a required field must be present
     * (not @ref FieldState::untouched); an integer field must parse and fit
     * `long long`; an enumeration choice must be in range; a timestamp must
     * parse as strict RFC 3339 Zulu; a string-map field must parse as `k=v`
     * pairs; and a member-less object field's raw buffer must look like a JSON
     * object. It never duplicates the registry's authoritative `ArgSpec` checks
     * (bounds, `one_of` groups, domain rules) - those come back as server
     * `invalid_field` errors the view assigns into @ref FormField::error
     * (C1/C7). Every field's previous advisory error is cleared first.
     *
     * @return `true` when no field has an advisory error; `false` otherwise.
     */
    bool validate();

    /**
     * @brief Builds the `args` object text the registry expects.
     *
     * Emits members in @ref ArgInfo descriptor order, including only fields that
     * are @ref FieldState::set (their encoded value) or @ref FieldState::cleared
     * (explicit `null`); @ref FieldState::untouched fields are omitted so the
     * handler applies defaults / leaves values unchanged. Strings, enum values,
     * timestamps and object members are escaped with `url_shortener::jsonEscape`
     * (every byte, including control characters and invalid UTF-8, is made
     * safe); integers are emitted unquoted; a set list/map is emitted as a JSON
     * array/object (empty `[]`/`{}` when the buffer parses to nothing, an
     * explicit replace-with-empty); an object is emitted with only its non-empty
     * members and omitted entirely when all members are empty. The result is
     * always well-formed JSON; callers should @ref validate first, as an invalid
     * integer buffer is emitted as a JSON string (so the server returns a clean
     * `invalid_field`) and an unparseable map/raw-object degrades to `{}` / is
     * omitted rather than corrupting the envelope.
     *
     * @return The `args` object as compact JSON text, e.g. `{"slug":"docs"}`.
     */
    std::string to_args_json() const;

    /**
     * @brief Builds the full v1 request envelope for submission.
     *
     * @param request_id Correlation id to echo; placed verbatim in
     * @ref commands::CommandRequest::id.
     * @return A request with `v == 1`, this form's @ref CommandInfo::name as the
     * command, @ref to_args_json as the args, and `options.dry_run` set only
     * when the dry-run toggle is on and the command supports it.
     */
    commands::CommandRequest to_request(const std::string& request_id) const;

    /**
     * @brief Builds the D5 footer summary of touched fields' validity.
     *
     * Lists each touched field (@ref FieldState::set or @ref FieldState::cleared)
     * as `"<name> ok"` when it has no advisory error or `"<name>: <message>"`
     * when it does, joined with `" - "` (e.g. `"target_url ok - slug ok"`). The
     * values embedded are server-supplied argument names, so the whole line must
     * be sanitized at the render sink (T-110). Call @ref validate first to
     * populate the messages.
     *
     * @return The summary line; empty when no field is touched.
     */
    std::string validation_summary() const;

    /**
     * @brief Prefills the form from a command `result` payload for the edit flow.
     *
     * Reads each field's value out of @p result_json by its @ref ArgInfo::name
     * using the sanctioned `json_view` accessors (no JSON tokenizing here,
     * contract C9) and loads it into the matching buffer: strings/timestamps
     * directly, integers via decimal text, booleans into the checkbox, enum
     * values by matching an index, string lists joined with `", "`, string maps
     * as `k=v` pairs, and object members by name. A required identity field that
     * is present (e.g. the `slug`) is marked @ref FieldState::set and
     * @ref FormField::locked so it is shown read-only and re-sent; every other
     * prefilled field is left @ref FieldState::untouched, so an unchanged field
     * is not re-sent and PATCH semantics are preserved. A list/map value that
     * contains a `,`/`=` and so cannot round-trip through the comma/equals form
     * gets an advisory @ref FormField::error pointing at the JSON escape hatch
     * rather than a corrupted buffer. Values are copied verbatim (not sanitized);
     * the view sanitizes them at the render sink (T-110).
     *
     * @param result_json The command `result` object text (e.g. a `LinkView`);
     * malformed or missing fields are simply skipped.
     */
    void prefill_from_result(const std::string& result_json);

private:
    /**
     * @brief Encodes one field's value for @ref to_args_json.
     *
     * @param field The field to encode.
     * @return The field's JSON value text (e.g. `"docs"`, `42`, `null`,
     * `["a"]`), or `std::nullopt` when the field is omitted entirely (untouched,
     * or an object whose members are all empty).
     */
    std::optional<std::string> encode_field(const FormField& field) const;

    ///< The command schema this form edits; owns its strings.
    CommandInfo m_command_;

    ///< Per-field editing state, parallel to @ref CommandInfo::args.
    std::vector<FormField> m_fields_;

    ///< Construction options (dry-run toggle, request-id prefix).
    FormOptions m_options_;

    ///< The dry-run toggle state set by @ref set_dry_run.
    bool m_dry_run_ = false;
};

}  // namespace url_shortener::tui
