#include "url_shortener/tui/router.hpp"

namespace url_shortener::tui
{

namespace
{

/// Maps a D1 tab key to its tab-root screen; returns false for a non-tab key.
bool tab_for_key(char key, ScreenId& out)
{
    switch (key)
    {
    case '1': out = ScreenId::dashboard; return true;
    case '2': out = ScreenId::links; return true;
    case '3': out = ScreenId::tables; return true;
    case '4': out = ScreenId::import_export; return true;
    case '5': out = ScreenId::jobs; return true;
    case '6': out = ScreenId::analytics; return true;
    case '7': out = ScreenId::settings; return true;
    case ':': out = ScreenId::palette; return true;
    default: return false;
    }
}

}  // namespace

Router::Router() : m_stack_ {ScreenId::dashboard}
{
}

ScreenId Router::current() const
{
    return m_stack_.back();
}

const std::vector<ScreenId>& Router::stack() const
{
    return m_stack_;
}

ScreenId Router::active_tab() const
{
    // The Confirm modal highlights the tab it returns to, not itself; every
    // other screen highlights its tab root, which is always the stack front.
    if (current() == ScreenId::confirm)
    {
        return m_confirm_return_;
    }
    return m_stack_.front();
}

bool Router::navigate_tab(char key)
{
    ScreenId tab = ScreenId::dashboard;
    if (!tab_for_key(key, tab))
    {
        return false;
    }
    // Tabs are global: drop any nested screens and sit at the tab root.
    m_stack_.assign(1, tab);
    return true;
}

bool Router::push(ScreenId next)
{
    if (!is_legal(current(), next))
    {
        return false;
    }

    // Resolving the Confirm modal returns to a tab root, leaving no residue.
    if (current() == ScreenId::confirm)
    {
        m_stack_.assign(1, next);
        return true;
    }

    // Record where a freshly-opened confirm will return: Dashboard for the
    // palette/generic-form path, Links for the link-form path (D11).
    if (next == ScreenId::confirm)
    {
        m_confirm_return_ = current() == ScreenId::generic_form
                                ? ScreenId::dashboard
                                : ScreenId::links;
    }

    m_stack_.push_back(next);
    return true;
}

bool Router::pop()
{
    if (m_stack_.size() <= 1)
    {
        return false;  // no-op at a tab root
    }
    m_stack_.pop_back();
    return true;
}

bool Router::is_legal(ScreenId from, ScreenId to)
{
    switch (from)
    {
    case ScreenId::dashboard:
        return to == ScreenId::links || to == ScreenId::tables
            || to == ScreenId::import_export || to == ScreenId::jobs
            || to == ScreenId::palette;
    case ScreenId::links:
        return to == ScreenId::link_detail || to == ScreenId::link_form;
    case ScreenId::link_detail:
        return to == ScreenId::link_form;
    case ScreenId::link_form:
        return to == ScreenId::confirm;
    case ScreenId::confirm:
        return to == ScreenId::links || to == ScreenId::dashboard;
    case ScreenId::tables:
        return to == ScreenId::row_json || to == ScreenId::link_form;
    case ScreenId::import_export:
        return to == ScreenId::jobs;
    case ScreenId::palette:
        return to == ScreenId::generic_form;
    case ScreenId::generic_form:
        return to == ScreenId::confirm;
    case ScreenId::jobs:
        return to == ScreenId::job_detail;
    case ScreenId::row_json:
    case ScreenId::job_detail:
    case ScreenId::analytics:
    case ScreenId::settings:
        return false;
    }
    return false;
}

}  // namespace url_shortener::tui
