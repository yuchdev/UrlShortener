#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace url_shortener::tui
{

/**
 * @file result_viewer_model.hpp
 * @brief The FTXUI-free model behind the Command Palette result viewer (D9/D11,
 * subtask 06.0/11): @ref ResultViewerModel pretty-prints a command's raw
 * `result` / `job` / `error` JSON into width-wrapped, scrollable lines.
 *
 * It belongs to `url_shortener_tui_model` (ADR 0003 rule 6): no FTXUI, no
 * colour. It is a **T-110 render source** over untrusted stored data (the raw
 * JSON of a stored link, a row, a job's failure text): @ref visible_lines runs
 * every emitted line through `text_fit::sanitize_for_terminal`, so no control /
 * bidi / zero-width byte reaches the grid.
 *
 * Bounded by construction (threat model T-121): a naive pretty-print of an
 * 8 MiB result can amplify to gigabytes (indent x depth per line). The model
 * therefore pretty-prints only inputs within @ref k_pretty_max_input (larger
 * results are shown as their compact source instead), clamps the indent through
 * `json_view::pretty_print`, wraps lazily to a bounded display width, and caps
 * the materialized line count at @ref k_max_display_lines - so both the memory
 * it holds and the per-frame cost of @ref visible_lines stay bounded regardless
 * of the result's size or nesting.
 */

/**
 * @brief A scrollable, width-wrapped, sanitized view of one command result's
 * JSON (D9 result viewer).
 *
 * Built once from the raw JSON text and a target display width; @ref scroll and
 * @ref page move a viewport over the wrapped lines and @ref visible_lines
 * returns the sanitized slice for a given height. Single-threaded: created and
 * mutated only on the UI thread.
 */
class ResultViewerModel
{
public:
    /**
     * @brief Largest input (in bytes) that is pretty-printed; a larger result is
     * shown as its compact source text so a deep/wide payload cannot amplify the
     * rendered size (threat model T-121).
     */
    static constexpr std::size_t k_pretty_max_input = 512u * 1024u;

    /**
     * @brief Upper bound on the number of wrapped display lines the model
     * materializes; content beyond this is dropped and @ref truncated reports
     * `true`, so memory stays bounded for any input.
     */
    static constexpr std::size_t k_max_display_lines = 20000u;

    /**
     * @brief Builds the viewer over a result's raw JSON.
     *
     * Pretty-prints @p json (two-space indent, clamped) when it is within
     * @ref k_pretty_max_input, otherwise keeps its compact source; splits the
     * text into logical lines, sanitizes each for the terminal, and wraps it to
     * @p width columns, materializing at most @ref k_max_display_lines lines.
     * Malformed or empty input is handled gracefully (an empty or verbatim
     * view), never thrown on.
     *
     * @param json The command `result` / `job` / `error` JSON text to display;
     * untrusted stored data, sanitized here before it is exposed.
     * @param width The target display width in columns; clamped to a sane
     * minimum/maximum so a degenerate width cannot break wrapping.
     */
    ResultViewerModel(std::string json, std::size_t width);

    /**
     * @brief Scrolls the viewport by @p delta lines (negative scrolls up),
     * clamped so the offset stays within the content.
     *
     * @param delta Lines to move; the resulting offset is clamped to
     * `[0, total_lines - 1]` (or `0` when empty).
     */
    void scroll(int delta);

    /**
     * @brief Scrolls by @p delta pages (a fixed page height), clamped like
     * @ref scroll.
     *
     * @param delta Pages to move (negative is up).
     */
    void page(int delta);

    /**
     * @brief The sanitized, width-fitted lines visible for a viewport of
     * @p height rows, starting at the current scroll offset.
     *
     * Each returned line is passed through `text_fit::sanitize_for_terminal`
     * (T-110) and is already width-wrapped, so it is safe to hand straight to an
     * FTXUI `text()`. Fewer than @p height lines are returned near the end of
     * the content.
     *
     * @param height The viewport height in rows.
     * @return Up to @p height sanitized display lines from the current offset.
     */
    std::vector<std::string> visible_lines(std::size_t height) const;

    /**
     * @brief The total number of wrapped display lines (bounded by
     * @ref k_max_display_lines).
     * @return The materialized line count.
     */
    std::size_t total_lines() const { return m_lines_.size(); }

    /**
     * @brief The current top-of-viewport line offset.
     * @return The scroll offset.
     */
    std::size_t offset() const { return m_offset_; }

    /**
     * @brief Whether content was dropped because it exceeded the line cap.
     * @return `true` when the result was larger than @ref k_max_display_lines
     * wrapped lines (the view shows a leading prefix only).
     */
    bool truncated() const { return m_truncated_; }

private:
    /// Pretty-prints or keeps @p json, then splits, sanitizes and wraps it into
    /// @ref m_lines_ (bounded by @ref k_max_display_lines).
    void build(const std::string& json);

    /// Appends @p safe_line (already sanitized) to @ref m_lines_ as one or more
    /// width-wrapped chunks, stopping at the line cap.
    void wrap_into_lines(const std::string& safe_line);

    ///< The target display width for wrapping, in columns (clamped).
    std::size_t m_width_;

    ///< The wrapped, sanitized display lines (each `<= m_width_` columns).
    std::vector<std::string> m_lines_;

    ///< Top-of-viewport offset into @ref m_lines_.
    std::size_t m_offset_ = 0;

    ///< `true` when the result exceeded the display-line cap.
    bool m_truncated_ = false;
};

}  // namespace url_shortener::tui
