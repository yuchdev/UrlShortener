#include "url_shortener/tui/toast.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

namespace
{

/// Maximum number of Unicode code points kept in a toast body for display; a
/// longer message is cut here and ends in an ellipsis. The full text is always
/// preserved in @ref Toast::raw_envelope.
constexpr std::size_t k_body_max_codepoints = 200;

/// Number of bytes in a UTF-8 sequence from its lead byte, or `1` for an
/// invalid lead (so the scan always advances and never splits forever).
std::size_t utf8_sequence_length(unsigned char lead)
{
    if ((lead & 0x80u) == 0x00u)
    {
        return 1;
    }
    if ((lead & 0xE0u) == 0xC0u)
    {
        return 2;
    }
    if ((lead & 0xF0u) == 0xE0u)
    {
        return 3;
    }
    if ((lead & 0xF8u) == 0xF0u)
    {
        return 4;
    }
    return 1;
}

/// Truncates @p text to at most @p max_codepoints code points, appending
/// `"..."` when content is dropped; never splits a UTF-8 sequence. The width
/// fit for the actual cell is a separate, render-time concern (text_fit).
std::string truncate_codepoints(const std::string& text,
                                std::size_t max_codepoints)
{
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < text.size() && count < max_codepoints)
    {
        i += utf8_sequence_length(static_cast<unsigned char>(text[i]));
        ++count;
    }
    if (i >= text.size())
    {
        return text;  // fits whole; no truncation
    }
    return text.substr(0, i) + "...";
}

}  // namespace

Toast toast_from_response(const std::string& command,
                          const std::string& request_id,
                          const commands::CommandResponse& r)
{
    Toast toast;
    toast.source_command = command;
    toast.request_id = request_id;
    // The raw envelope kept for [d]etails is exactly what the client returned
    // (success criterion 3), pretty-printed for readability; sanitized only at
    // the render sink (T-110).
    toast.raw_envelope =
        pretty_print(commands::serialize_response(r));

    if (r.ok)
    {
        toast.kind = ToastKind::success;
        toast.title = command.empty() ? std::string("ok") : command + " ok";
        return toast;
    }

    // Failure. An ambiguous/self-contradictory envelope (ok == false with
    // error.code == none) is reported as a generic internal error, never a
    // success (threat model T-113).
    commands::CommandErrorCode code = r.error.code;
    if (code == commands::CommandErrorCode::none)
    {
        code = commands::CommandErrorCode::internal;
    }

    const std::string_view name = commands::error_info_for(code).name;
    toast.kind = ToastKind::error;
    toast.title = "! " + std::string(name);
    toast.body = truncate_codepoints(r.error.message, k_body_max_codepoints);
    return toast;
}

ToastQueue::ToastQueue(const IClock& clock, std::chrono::seconds ttl)
    : m_clock_(clock), m_ttl_(ttl)
{
}

void ToastQueue::push(Toast t)
{
    if (t.kind != ToastKind::error)
    {
        t.expires = m_clock_.now() + m_ttl_;
    }
    m_toasts_.push_back(std::move(t));
    while (m_toasts_.size() > k_max_visible)
    {
        m_toasts_.pop_front();
    }
}

void ToastQueue::tick()
{
    const std::chrono::system_clock::time_point now = m_clock_.now();
    for (auto it = m_toasts_.begin(); it != m_toasts_.end();)
    {
        const bool auto_expires = it->kind != ToastKind::error;
        if (auto_expires && it->expires <= now)
        {
            it = m_toasts_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

const std::deque<Toast>& ToastQueue::visible() const
{
    return m_toasts_;
}

void ToastQueue::dismiss_front()
{
    if (!m_toasts_.empty())
    {
        m_toasts_.pop_front();
    }
}

}  // namespace url_shortener::tui
