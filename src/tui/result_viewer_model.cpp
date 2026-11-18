/**
 * @file result_viewer_model.cpp
 * @brief The Command Palette result viewer model (D9, subtask 06.0/11):
 * pretty-prints and width-wraps a command result's raw JSON into scrollable,
 * sanitized lines. FTXUI-free; bounded in memory and per-frame cost regardless
 * of input size or nesting (threat model T-121), and a T-110 render source (it
 * sanitizes every emitted line).
 */
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/result_viewer_model.hpp"

#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/text_fit.hpp>

namespace url_shortener::tui
{

namespace
{

/// Minimum and maximum wrapping width; a degenerate width cannot break wrapping
/// and an absurd one cannot inflate the per-line memory bound.
constexpr std::size_t k_min_width = 8u;
constexpr std::size_t k_max_width = 1024u;

/// Indent used when pretty-printing; `json_view::pretty_print` additionally
/// clamps it for defence in depth.
constexpr int k_indent = 2;

/// Maximum bytes of one logical line accumulated/sanitized per step, so a huge
/// single-line (compact) result is processed in bounded chunks (T-121).
constexpr std::size_t k_max_logical_bytes = 256u * 1024u;

/// Fixed page height used by @ref ResultViewerModel::page.
constexpr int k_page_lines = 20;

/// Byte length of the UTF-8 code point starting at @p s[@p pos]; always `>= 1`
/// and never past the end of @p s. @p s is already valid UTF-8 (post-sanitize).
std::size_t utf8_len(const std::string& s, std::size_t pos)
{
    const auto lead = static_cast<unsigned char>(s[pos]);
    std::size_t len = 1;
    if ((lead & 0x80u) == 0x00u) {
        len = 1;
    }
    else if ((lead & 0xE0u) == 0xC0u) {
        len = 2;
    }
    else if ((lead & 0xF0u) == 0xE0u) {
        len = 3;
    }
    else if ((lead & 0xF8u) == 0xF0u) {
        len = 4;
    }
    if (pos + len > s.size()) {
        len = s.size() - pos;
    }
    return len == 0 ? 1 : len;
}

}  // namespace

ResultViewerModel::ResultViewerModel(std::string json, std::size_t width)
    : m_width_(std::min(std::max(width, k_min_width), k_max_width))
{
    build(json);
}

void ResultViewerModel::build(const std::string& json)
{
    // Pretty-print only bounded inputs: a deep/wide multi-MiB result would
    // amplify to gigabytes if re-indented, so a large result keeps its compact
    // source and is merely wrapped (threat model T-121).
    const std::string text =
        json.size() <= k_pretty_max_input ? pretty_print(json, k_indent) : json;

    std::size_t i = 0;
    while (i < text.size() && m_lines_.size() < k_max_display_lines) {
        const std::size_t start = i;
        while (i < text.size() && text[i] != '\n'
               && (i - start) < k_max_logical_bytes)
        {
            ++i;
        }
        const std::string logical = text.substr(start, i - start);
        const bool hit_newline = i < text.size() && text[i] == '\n';
        if (hit_newline) {
            ++i;  // consume the line break
        }
        // Sanitize the whole logical line BEFORE width fitting, so a wrap can
        // never split and re-expose a half-stripped escape (threat model T-110).
        wrap_into_lines(
            sanitize_for_terminal(logical, k_max_logical_bytes));
    }
    if (i < text.size()) {
        m_truncated_ = true;  // stopped at the display-line cap
    }
}

void ResultViewerModel::wrap_into_lines(const std::string& safe_line)
{
    if (safe_line.empty()) {
        if (m_lines_.size() < k_max_display_lines) {
            m_lines_.emplace_back();  // preserve a blank logical line
        }
        return;
    }
    std::size_t pos = 0;
    while (pos < safe_line.size() && m_lines_.size() < k_max_display_lines) {
        const std::size_t chunk_start = pos;
        std::size_t cols = 0;
        while (pos < safe_line.size()) {
            const std::size_t len = utf8_len(safe_line, pos);
            const std::size_t cp_cols =
                display_width(safe_line.substr(pos, len));
            if (cols != 0 && cols + cp_cols > m_width_) {
                break;  // this code point would overflow the row
            }
            cols += cp_cols;
            pos += len;
            if (cols >= m_width_) {
                break;  // row is full
            }
        }
        m_lines_.push_back(safe_line.substr(chunk_start, pos - chunk_start));
    }
    if (pos < safe_line.size()) {
        m_truncated_ = true;
    }
}

void ResultViewerModel::scroll(int delta)
{
    const std::size_t max_offset =
        m_lines_.empty() ? 0u : m_lines_.size() - 1u;
    if (delta < 0) {
        const auto up = static_cast<std::size_t>(-delta);
        m_offset_ = up >= m_offset_ ? 0u : m_offset_ - up;
    }
    else {
        m_offset_ = std::min(m_offset_ + static_cast<std::size_t>(delta),
                             max_offset);
    }
}

void ResultViewerModel::page(int delta)
{
    scroll(delta * k_page_lines);
}

std::vector<std::string> ResultViewerModel::visible_lines(
    std::size_t height) const
{
    std::vector<std::string> out;
    for (std::size_t i = m_offset_; i < m_lines_.size() && out.size() < height;
         ++i)
    {
        // Idempotent re-sanitize: the stored lines are already safe, but the
        // viewer's contract is that every emitted line passes the T-110 sink.
        out.push_back(sanitize_for_terminal(m_lines_[i]));
    }
    return out;
}

}  // namespace url_shortener::tui
