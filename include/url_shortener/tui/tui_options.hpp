#pragma once

#include <cstddef>

namespace url_shortener::tui
{

/**
 * @brief The colour theme to render with (dark / light / mono).
 *
 * Forward-declared here with a fixed underlying type so @ref TuiOptions can
 * hold a value before subtask 05 defines the enumerators. When 05 lands it adds
 * the definition (and this header includes `theme.hpp`); until then the field
 * is only stored and passed through, never inspected.
 */
enum class ThemeName : int;

/**
 * @brief Process-level TUI options, created once at startup (subtask 19 maps
 * CLI flags onto it) and carried read-only through @ref ScreenContext.
 *
 * A plain value type: it holds presentation and safety preferences, never a
 * client, registry or secret. Authorization is still the registry's job
 * (@ref SessionInfo::read_only is only a presentation hint, contract C7).
 */
struct TuiOptions
{
    ///< Active colour theme. Value-initialised until subtask 05 wires the enum;
    ///< this subtask neither reads nor renders from it.
    ThemeName theme {};

    ///< `true` to run the session as `ControlSet::User`: write controls render
    ///< disabled, not hidden (ADR 0003 rule 5, subtask 18).
    bool read_only = false;

    ///< `true` to skip mutation confirm dialogs (`--yes`), for scripted/test
    ///< runs (ADR 0003 rule 4, subtask 06).
    bool assume_yes = false;

    ///< Maximum bytes of an inline import/paste payload before a file path is
    ///< required (subtask 15); a DoS bound (threat model T-121).
    std::size_t max_inline_bytes = 65536;
};

}  // namespace url_shortener::tui
