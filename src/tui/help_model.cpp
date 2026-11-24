#include "url_shortener/tui/help_model.hpp"

#include <unordered_set>
#include <utility>

namespace url_shortener::tui
{

HelpModel::HelpModel(std::vector<KeyHint> global, std::vector<KeyHint> screen)
    : m_global_(std::move(global)), m_screen_(std::move(screen))
{
}

std::vector<HelpEntry> HelpModel::entries() const
{
    std::vector<HelpEntry> out;
    out.reserve(m_global_.size() + m_screen_.size());

    // A duplicate is an entry whose keys and label both repeat one already
    // emitted; the '\x1f' unit separator can appear in neither field, so it is
    // a safe join for the identity key.
    std::unordered_set<std::string> seen;
    const auto append = [&](const std::vector<KeyHint>& hints) {
        for (const KeyHint& hint : hints)
        {
            std::string identity = hint.keys;
            identity.push_back('\x1f');
            identity += hint.label;
            if (seen.insert(std::move(identity)).second)
            {
                out.push_back(HelpEntry {hint.keys, hint.label});
            }
        }
    };

    append(m_global_);
    append(m_screen_);
    return out;
}

}  // namespace url_shortener::tui
