#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <url_shortener/tui/capability.hpp>
#include <url_shortener/tui/command_client.hpp>

namespace url_shortener::tui
{

/**
 * @file palette_model.hpp
 * @brief The FTXUI-free model behind the Command Palette (D9, subtask
 * 06.0/11): @ref PaletteModel ranks and filters whatever
 * @ref ICommandClient::commands returns so every registry command is reachable
 * with no bespoke code (ADR 0003 Decision 2 rule 2).
 *
 * It belongs to `url_shortener_tui_model` (ADR 0003 rule 6): no FTXUI, no
 * colour, no JSON tokenizing. It never consults authorization (contract C7) -
 * the read-only *disabling* it computes is a presentation hint for the view
 * (ADR 0003 rule 5), and the registry still enforces every permission. It names
 * no command (contract C1/C8): the "(confirm)" marker is derived through the
 * one confirmation authority (@ref ConfirmPolicy), never a hard-coded command
 * list. Every display string it exposes (@ref PaletteEntry's command name /
 * summary / tag) is raw server data the view must pass through
 * `text_fit::sanitize_for_terminal` before rendering (threat model T-110).
 */

/**
 * @brief One ranked palette row: the command, its display tag, and its
 * enabled/score state.
 *
 * A lightweight view over a @ref CommandInfo owned by the producing
 * @ref PaletteModel. @ref info is a non-owning pointer into the model's stable
 * command vector and is valid only for the lifetime of that model; copy the
 * @ref CommandInfo out if it must outlive the model.
 */
struct PaletteEntry
{
    ///< The command this row represents; a non-owning pointer into the owning
    ///< @ref PaletteModel's command vector (never null for a returned entry).
    const CommandInfo* info = nullptr;

    ///< The display tag, e.g. `"read"`, `"write"`, `"write (confirm)"` or
    ///< `"read (jobs)"`: the base permission plus a `(jobs)` marker for a
    ///< job-backed command and a `(confirm)` marker for one that needs a typed
    ///< confirmation. Raw composition text; still sanitized at the render sink.
    std::string tag;

    ///< `true` when the command may be activated in the current session;
    ///< `false` for a write/migrate command in a read-only session, which the
    ///< view renders dim and cannot open (ADR 0003 rule 5).
    bool enabled = true;

    ///< Why the entry is disabled (shown dim beside it), e.g.
    ///< `"not_authorized (read-only session)"`; empty when @ref enabled.
    std::string disabled_reason;

    ///< The match score for the current query; higher ranks first. `0` means
    ///< hidden (the entry is excluded from @ref PaletteModel::entries).
    int score = 0;
};

/**
 * @brief Ranks and filters the command catalog for the Command Palette (D9).
 *
 * Built once from @ref ICommandClient::commands, it owns a stable copy of the
 * catalog so a command added to the registry later appears with no TUI code
 * change (D9 "zero TUI code"). @ref set_query re-ranks the entries; @ref
 * entries returns the visible, ranked subset (score descending, then command
 * name ascending for a stable order); @ref select_next / @ref select_prev move
 * the highlighted row within that subset.
 *
 * Single-threaded: a `PaletteModel` is created and mutated only on the UI
 * thread.
 */
class PaletteModel
{
public:
    /**
     * @brief Builds the model from a catalog snapshot.
     *
     * Copies @p commands into a stable internal vector (so @ref PaletteEntry
     * pointers stay valid) and computes each command's tag and enabled state
     * once. Starts with an empty query, so @ref entries initially lists every
     * command in name order.
     *
     * @param commands The command catalog, typically @ref
     * ICommandClient::commands; moved in and retained for the model's lifetime.
     * @param capability The single capability decision (subtask 18): in a
     * read-only session each entry whose command is not runnable is marked
     * disabled with the capability's reason (ADR 0003 rule 5). A presentation
     * mirror only; authorization stays with the registry (C7).
     */
    PaletteModel(std::vector<CommandInfo> commands, CapabilityModel capability);

    /**
     * @brief Sets the filter query and re-ranks the entries.
     *
     * A single leading `:` (the palette prefix) and surrounding whitespace are
     * stripped before matching. An empty query lists every command; a query
     * that matches nothing yields an empty @ref entries list. Selection is
     * reset to the first (highest-ranked) entry.
     *
     * @param q The raw filter text from the palette input box.
     */
    void set_query(std::string q);

    /**
     * @brief The current (stripped) query the entries are ranked for.
     * @return The query with its leading `:`/whitespace removed.
     */
    const std::string& query() const { return m_query_; }

    /**
     * @brief The ranked, visible entries for the current query.
     *
     * Ordered by @ref PaletteEntry::score descending then command name
     * ascending, so equal scores have a deterministic, stable order. Each
     * entry's @ref PaletteEntry::info points into this model and is valid until
     * the model is destroyed.
     *
     * @return The visible entries; empty when nothing matches the query.
     */
    const std::vector<PaletteEntry>& entries() const { return m_entries_; }

    /**
     * @brief Moves the selection to the next entry (clamped at the last).
     */
    void select_next();

    /**
     * @brief Moves the selection to the previous entry (clamped at the first).
     */
    void select_prev();

    /**
     * @brief The index of the highlighted entry within @ref entries.
     * @return The selection index, or `0` when @ref entries is empty.
     */
    std::size_t selected_index() const { return m_selected_; }

    /**
     * @brief The currently highlighted entry.
     * @return A pointer to the selected @ref PaletteEntry, or `nullptr` when
     * @ref entries is empty. Valid until the next @ref set_query.
     */
    const PaletteEntry* selected() const;

    /**
     * @brief Scores a command against a query (the ranking primitive).
     *
     * Tiers, highest first: an exact name match; a name prefix (so `link.`
     * selects only the `link.*` commands); a dotted-segment prefix (a segment
     * after a `.` begins with the query); a subsequence of the name (so `lst`
     * matches `link.list`); and finally a substring of the summary. A non-empty
     * query that matches none of these scores `0` (hidden). An empty query
     * scores every command equally (all visible), so @ref entries then lists
     * them in name order.
     *
     * @param query The (already stripped) filter text.
     * @param c The command to score; its @ref CommandInfo::name and
     * @ref CommandInfo::summary are read.
     * @return A positive score for a match (higher is a better match), or `0`
     * when @p query is non-empty and does not match @p c.
     */
    static int score(const std::string& query, const CommandInfo& c);

private:
    /// Recomputes @ref m_entries_ from @ref m_commands_ for @ref m_query_.
    void rebuild();

    ///< The stable catalog copy; @ref PaletteEntry::info points into this and
    ///< it is never reallocated after construction.
    std::vector<CommandInfo> m_commands_;

    ///< The single capability decision; drives per-entry disabling (subtask
    ///< 18).
    CapabilityModel m_capability_;

    ///< The display tag for each command, parallel to @ref m_commands_
    ///< (computed once; "(confirm)"/"(jobs)" markers folded in).
    std::vector<std::string> m_tags_;

    ///< The current stripped query.
    std::string m_query_;

    ///< The ranked, visible entries for @ref m_query_.
    std::vector<PaletteEntry> m_entries_;

    ///< Selection index into @ref m_entries_; clamped to a valid row.
    std::size_t m_selected_ = 0;
};

}  // namespace url_shortener::tui
