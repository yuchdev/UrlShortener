/**
 * @file palette_model.cpp
 * @brief The Command Palette ranking model (D9, subtask 06.0/11): turns the
 * command catalog into ranked, filtered @ref url_shortener::tui::PaletteEntry
 * rows. FTXUI-free and command-name-free (contracts C1/C8): the "(confirm)"
 * marker is derived through the single confirmation authority
 * (@ref url_shortener::tui::ConfirmPolicy), never a hard-coded command list.
 */
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/palette_model.hpp"

#include <url_shortener/tui/confirm.hpp>

namespace url_shortener::tui
{

namespace
{

/// Lower-cases an ASCII string for case-insensitive matching (command names are
/// already lower-case; summaries may be mixed-case).
std::string to_lower(const std::string& s)
{
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

/// Whether @p s starts with @p prefix.
bool starts_with(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size()
        && s.compare(0, prefix.size(), prefix) == 0;
}

/// Whether @p needle appears as an in-order (not necessarily contiguous)
/// subsequence of @p haystack.
bool is_subsequence(const std::string& needle, const std::string& haystack)
{
    if (needle.empty()) {
        return true;
    }
    std::size_t j = 0;
    for (std::size_t i = 0; i < haystack.size() && j < needle.size(); ++i) {
        if (haystack[i] == needle[j]) {
            ++j;
        }
    }
    return j == needle.size();
}

/// Whether a dotted-name segment of @p name begins with @p query (e.g. `create`
/// in `link.create`).
bool segment_prefix(const std::string& name, const std::string& query)
{
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t dot = name.find('.', start);
        const std::size_t end = dot == std::string::npos ? name.size() : dot;
        const std::string segment = name.substr(start, end - start);
        if (starts_with(segment, query)) {
            return true;
        }
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return false;
}

/// Whether invoking @p c would require a typed-token confirmation (the slug of a
/// `link.delete`, the `MIGRATE` token of a migrate command). Asked through the
/// single confirmation authority with a representative slug probe so the
/// policy's slug-gated branch reports `type_token`, keeping the palette free of
/// any command-name list (contract C1/C8).
bool needs_typed_confirm(const CommandInfo& c)
{
    const ConfirmPolicy policy(/*assume_yes=*/false);
    const ConfirmRequest decision = policy.decide(c, R"({"slug":"x"})");
    return decision.kind == ConfirmKind::type_token;
}

/// Builds a command's display tag: the base permission plus a `(jobs)` marker
/// for a job-backed command and a `(confirm)` marker for one needing a typed
/// confirmation (e.g. `"write (confirm)"`, `"read (jobs)"`).
std::string make_tag(const CommandInfo& c)
{
    std::string tag = c.permission.empty() ? std::string("read") : c.permission;
    std::vector<std::string> markers;
    if (c.is_job) {
        markers.push_back("jobs");
    }
    if (needs_typed_confirm(c)) {
        markers.push_back("confirm");
    }
    if (!markers.empty()) {
        tag += " (";
        for (std::size_t i = 0; i < markers.size(); ++i) {
            if (i != 0) {
                tag += ", ";
            }
            tag += markers[i];
        }
        tag += ")";
    }
    return tag;
}

}  // namespace

PaletteModel::PaletteModel(std::vector<CommandInfo> commands, bool read_only)
    : m_commands_(std::move(commands))
    , m_read_only_(read_only)
{
    m_tags_.reserve(m_commands_.size());
    for (const CommandInfo& c : m_commands_) {
        m_tags_.push_back(make_tag(c));
    }
    rebuild();
}

int PaletteModel::score(const std::string& query, const CommandInfo& c)
{
    if (query.empty()) {
        // An empty query lists every command; entries() then orders them by
        // name (all scores equal).
        return 1;
    }
    const std::string q = to_lower(query);
    const std::string name = to_lower(c.name);
    if (name == q) {
        return 1000;
    }
    if (starts_with(name, q)) {
        return 800;
    }
    if (segment_prefix(name, q)) {
        return 600;
    }
    if (is_subsequence(q, name)) {
        return 400;
    }
    if (to_lower(c.summary).find(q) != std::string::npos) {
        return 200;
    }
    return 0;
}

void PaletteModel::set_query(std::string q)
{
    // Strip a single leading ':' (the palette prefix) and surrounding spaces.
    std::size_t begin = 0;
    std::size_t end = q.size();
    while (begin < end && (q[begin] == ' ' || q[begin] == '\t')) {
        ++begin;
    }
    if (begin < end && q[begin] == ':') {
        ++begin;
    }
    while (begin < end && (q[begin] == ' ' || q[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (q[end - 1] == ' ' || q[end - 1] == '\t')) {
        --end;
    }
    m_query_ = q.substr(begin, end - begin);
    rebuild();
}

void PaletteModel::rebuild()
{
    m_entries_.clear();
    for (std::size_t i = 0; i < m_commands_.size(); ++i) {
        const int s = score(m_query_, m_commands_[i]);
        if (s <= 0) {
            continue;
        }
        PaletteEntry entry;
        entry.info = &m_commands_[i];
        entry.tag = m_tags_[i];
        entry.score = s;
        const bool is_read = m_commands_[i].permission == "read"
            || m_commands_[i].permission.empty();
        entry.enabled = !(m_read_only_ && !is_read);
        if (!entry.enabled) {
            entry.disabled_reason = "not_authorized (read-only session)";
        }
        m_entries_.push_back(std::move(entry));
    }
    std::sort(m_entries_.begin(), m_entries_.end(),
              [](const PaletteEntry& a, const PaletteEntry& b) {
                  if (a.score != b.score) {
                      return a.score > b.score;
                  }
                  return a.info->name < b.info->name;
              });
    m_selected_ = 0;
}

void PaletteModel::select_next()
{
    if (m_entries_.empty()) {
        m_selected_ = 0;
        return;
    }
    if (m_selected_ + 1 < m_entries_.size()) {
        ++m_selected_;
    }
}

void PaletteModel::select_prev()
{
    if (m_selected_ > 0) {
        --m_selected_;
    }
}

const PaletteEntry* PaletteModel::selected() const
{
    if (m_entries_.empty() || m_selected_ >= m_entries_.size()) {
        return nullptr;
    }
    return &m_entries_[m_selected_];
}

}  // namespace url_shortener::tui
