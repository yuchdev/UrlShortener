/**
 * @file import_model.cpp
 * @brief Implementation of the Import / Export view-models (D7, subtask
 * 06.0/15): the wire-value mappings, the advisory paste pre-check, the blocking
 * guard and the `link.import` / `link.export` request builders. FTXUI-free and
 * JSON built only through `url_shortener::jsonString` (contract C9).
 */
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/import_model.hpp"

#include <url_shortener/core/utils.hpp>

namespace url_shortener::tui
{

namespace
{

/// Most malformed-line warnings the advisory preview will collect; a bound so a
/// large hostile paste cannot produce an unbounded warning list (T-121).
constexpr std::size_t k_max_warnings = 20;

/// Trims leading/trailing ASCII whitespace (space, tab, CR, LF, FF, VT) from a
/// view of @p s, returning the trimmed substring.
std::string trim_ascii_ws(const std::string& s)
{
    const char* ws = " \t\r\n\f\v";
    const std::size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return std::string();
    }
    const std::size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

}  // namespace

std::string format_to_wire(ImportFormat format)
{
    return format == ImportFormat::ndjson ? "ndjson" : "csv";
}

bool append_within_cap(std::string& buffer,
                       const std::string& addition,
                       std::size_t cap)
{
    if (buffer.size() >= cap) {
        return false;  // already full: an O(1) no-op (CWE-407)
    }
    std::size_t take = cap - buffer.size();
    if (take > addition.size()) {
        take = addition.size();
    }
    // Do not split a multibyte code point at the cut.
    while (take > 0 && take < addition.size()
           && (static_cast<unsigned char>(addition[take]) & 0xC0u) == 0x80u)
    {
        --take;
    }
    if (take == 0) {
        return false;
    }
    buffer.append(addition, 0, take);
    return true;
}

std::string on_conflict_to_wire(OnConflict on_conflict)
{
    switch (on_conflict) {
        case OnConflict::fail:
            return "fail";
        case OnConflict::overwrite:
            return "overwrite";
        case OnConflict::skip:
            break;
    }
    return "skip";
}

ImportModel::ImportModel(std::size_t max_inline_bytes)
    : m_max_inline_bytes_(max_inline_bytes)
{
}

void ImportModel::set_source(ImportSource source)
{
    m_source_ = source;
}

void ImportModel::set_format(ImportFormat format)
{
    m_format_ = format;
}

void ImportModel::set_on_conflict(OnConflict on_conflict)
{
    m_on_conflict_ = on_conflict;
}

void ImportModel::set_text(std::string text)
{
    m_text_ = std::move(text);
}

void ImportModel::set_path(std::string path)
{
    m_path_ = std::move(path);
}

void ImportModel::set_dry_run(bool dry_run)
{
    m_dry_run_ = dry_run;
}

ImportModel::Preview ImportModel::preview() const
{
    Preview result;
    if (m_source_ == ImportSource::file || m_text_.empty()) {
        return result;
    }

    const bool is_csv = m_format_ == ImportFormat::csv;
    std::size_t line_no = 0;
    std::size_t start = 0;
    while (start <= m_text_.size()) {
        const std::size_t nl = m_text_.find('\n', start);
        const std::size_t end = nl == std::string::npos ? m_text_.size() : nl;
        std::string line = m_text_.substr(start, end - start);
        ++line_no;

        const std::string trimmed = trim_ascii_ws(line);
        if (!trimmed.empty()) {
            ++result.lines;
            if (is_csv) {
                const std::size_t comma = trimmed.find(',');
                if (comma == std::string::npos) {
                    if (result.warnings.size() < k_max_warnings) {
                        result.warnings.push_back(
                            "line " + std::to_string(line_no)
                            + ": no comma (expected full_url,slug)");
                    }
                }
                else {
                    const std::string slug =
                        trim_ascii_ws(trimmed.substr(comma + 1));
                    if (slug.empty()) {
                        ++result.blank_slugs;
                    }
                }
            }
        }

        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return result;
}

std::optional<std::string> ImportModel::blocking_error() const
{
    if (m_source_ == ImportSource::paste) {
        if (m_text_.empty()) {
            return std::string("paste is empty; add pairs or use a file path");
        }
        if (m_text_.size() > m_max_inline_bytes_) {
            return std::string(
                "pasted payload exceeds the inline limit; use a file path");
        }
        return std::nullopt;
    }
    // File source.
    if (trim_ascii_ws(m_path_).empty()) {
        return std::string("file path is empty");
    }
    return std::nullopt;
}

commands::CommandRequest ImportModel::to_request(
    const std::string& request_id) const
{
    commands::CommandRequest request;
    request.v = 1;
    request.id = request_id;
    request.command = "link.import";

    std::string args = "{\"format\":";
    args += url_shortener::jsonString(format_to_wire(m_format_));
    if (m_source_ == ImportSource::paste) {
        args += ",\"data\":";
        args += url_shortener::jsonString(m_text_);
    }
    else {
        args += ",\"path\":";
        args += url_shortener::jsonString(m_path_);
    }
    args += ",\"on_conflict\":";
    args += url_shortener::jsonString(on_conflict_to_wire(m_on_conflict_));
    args += "}";

    request.args_json = std::move(args);
    request.options.dry_run = m_dry_run_;
    return request;
}

void ExportModel::set_format(ImportFormat format)
{
    m_format_ = format;
}

void ExportModel::set_filter(const LinkListFilter& filter)
{
    m_filter_ = filter;
}

void ExportModel::set_path(std::string path)
{
    m_path_ = std::move(path);
}

commands::CommandRequest ExportModel::to_request(
    const std::string& request_id) const
{
    commands::CommandRequest request;
    request.v = 1;
    request.id = request_id;
    request.command = "link.export";

    std::string args = "{\"format\":";
    args += url_shortener::jsonString(format_to_wire(m_format_));
    args += ",\"status\":";
    args += url_shortener::jsonString(status_to_wire(m_filter_.status));
    if (!m_filter_.q.empty()) {
        args += ",\"q\":";
        args += url_shortener::jsonString(m_filter_.q);
    }
    if (!m_filter_.tag.empty()) {
        args += ",\"tag\":";
        args += url_shortener::jsonString(m_filter_.tag);
    }
    if (!m_filter_.campaign.empty()) {
        args += ",\"campaign\":";
        args += url_shortener::jsonString(m_filter_.campaign);
    }
    if (!m_path_.empty()) {
        args += ",\"path\":";
        args += url_shortener::jsonString(m_path_);
    }
    args += "}";

    request.args_json = std::move(args);
    return request;
}

}  // namespace url_shortener::tui
