#include "url_shortener/tui/app.hpp"

#include <cstdio>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/frame_model.hpp>
#include <url_shortener/tui/frame_view.hpp>
#include <url_shortener/tui/ftxui_poster.hpp>
#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/screens/placeholder_screen.hpp>
#include <url_shortener/tui/terminal_restore.hpp>

namespace url_shortener::tui
{

namespace
{

/// Returns the file descriptor behind `stdout`, for the terminal-restore sink.
int stdout_fd()
{
#if defined(_WIN32)
    return ::_fileno(stdout);
#else
    return ::fileno(stdout);
#endif
}

/// A tab slot and the placeholder text shown until a real screen registers it.
struct TabSlot
{
    ScreenId id;
    const char* message;
};

/// Default placeholder bodies for the eight D1 tabs. Analytics is permanent
/// (ADR 0003 non-goal); the rest are temporary until subtasks 11-17/21 land.
const TabSlot k_tab_slots[] = {
    {ScreenId::dashboard, "Dashboard: not implemented"},
    {ScreenId::links, "Links: not implemented"},
    {ScreenId::tables, "Tables: not implemented"},
    {ScreenId::import_export, "Import/Export: not implemented"},
    {ScreenId::jobs, "Jobs: not implemented"},
    {ScreenId::analytics, "Analytics: provided by Milestone 0002"},
    {ScreenId::settings, "Settings: not implemented"},
    {ScreenId::palette, "Command palette: not implemented"},
};

}  // namespace

App::App(ICommandClient& client, TuiOptions options)
    : m_client_(client), m_options_(std::move(options))
{
}

void App::register_screen(std::unique_ptr<IScreen> screen)
{
    m_screens_.push_back(std::move(screen));
}

int App::run()
{
    using namespace ftxui;

    // Fill in a placeholder for any tab a real screen has not registered, so
    // the frame and navigation work before subtasks 11-17/21 land.
    for (const TabSlot& slot : k_tab_slots)
    {
        bool present = false;
        for (const std::unique_ptr<IScreen>& screen : m_screens_)
        {
            if (screen->id() == slot.id)
            {
                present = true;
                break;
            }
        }
        if (!present)
        {
            register_screen(make_placeholder_screen(slot.id, slot.message));
        }
    }

    auto screen = ScreenInteractive::Fullscreen();
    screen.TrackMouse(false);  // mouse is an ADR 0003 non-goal (T-124)

    // Teardown ordering (UAF safety): the caller is declared after the poster
    // and the screen, so it is destroyed first and joins its workers before the
    // poster/screen die - a late completion can never post to a dead screen.
    FtxuiPoster poster(screen);
    AsyncCaller caller(m_client_, poster);

    // Map a screen id to its index in the tab container.
    const auto index_for = [this](ScreenId id) -> int {
        for (std::size_t i = 0; i < m_screens_.size(); ++i)
        {
            if (m_screens_[i]->id() == id)
            {
                return static_cast<int>(i);
            }
        }
        return 0;
    };

    std::vector<Component> components;
    components.reserve(m_screens_.size());
    for (const std::unique_ptr<IScreen>& s : m_screens_)
    {
        components.push_back(s->component());
    }
    int active_index = 0;
    Component tabs = Container::Tab(std::move(components), &active_index);

    // Load the backend label/persistent flag without blocking the first paint.
    RequestIdGenerator ids;
    commands::CommandRequest info;
    info.command = "system.info";
    info.id = ids.next();
    caller.submit(info, [this](const commands::CommandResponse& resp) {
        if (!resp.ok)
        {
            if (m_backend_label_ == "connecting...")
            {
                m_backend_label_ = "unknown";
            }
            return;
        }
        // UNVERIFIED: the exact system.info shape is owned by 03.0/17; read the
        // backend label and persistence flag defensively (missing -> keep).
        if (std::optional<std::string> label =
                get_string(resp.result_json, "backend"))
        {
            m_backend_label_ = *label;
        }
        if (std::optional<bool> persistent =
                get_bool(resp.result_json, "persistent"))
        {
            m_persistent_ = *persistent;
        }
    });

    Component root = Renderer(tabs, [&] {
        const Dimensions size = Terminal::Size();
        active_index = index_for(m_router_.active_tab());
        FrameModel model(m_client_.session(), m_backend_label_, m_persistent_,
                         std::string());
        std::vector<KeyHint> hints;
        if (active_index >= 0
            && static_cast<std::size_t>(active_index) < m_screens_.size())
        {
            hints = m_screens_[active_index]->hints();
        }
        return render_frame(model, m_router_.active_tab(), tabs->Render(),
                            hints, size.dimx, size.dimy);
    });

    root = CatchEvent(root, [&](const Event& event) -> bool {
        // A focused text input swallows navigation keys (typing `1` in a slug
        // field must not switch tab).
        if (m_text_input_active_)
        {
            return false;
        }
        if (event == Event::CtrlC)
        {
            screen.Exit();
            return true;
        }
        if (event == Event::F1)
        {
            return true;  // help overlay toggle lands in subtask 05
        }
        if (event == Event::Escape)
        {
            return m_router_.pop();
        }
        if (event.is_character() && event.character().size() == 1)
        {
            const char key = event.character()[0];
            if ((key >= '1' && key <= '7') || key == ':')
            {
                return m_router_.navigate_tab(key);
            }
            if (key == 'q')
            {
                return m_router_.pop();
            }
        }
        return false;
    });

    FdTerminalSink sink(stdout_fd());
    TerminalRestore restore(sink);
    ScopedSignalRestore signal_restore(restore);

    run_with_restore(restore, [&] { screen.Loop(root); });
    return 0;
}

}  // namespace url_shortener::tui
