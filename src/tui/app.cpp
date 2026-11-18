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
#include <url_shortener/tui/help_model.hpp>
#include <url_shortener/tui/help_overlay.hpp>
#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/screens/placeholder_screen.hpp>
#include <url_shortener/tui/system_info_view.hpp>
#include <url_shortener/tui/terminal_restore.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

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

/// Returns the file descriptor behind `stdin`, for the termios-restore hook.
int stdin_fd()
{
#if defined(_WIN32)
    return ::_fileno(stdin);
#else
    return ::fileno(stdin);
#endif
}

/// The global keymap listed first in the F1 help overlay (subtask 05). These
/// are composition-fixed labels, never server data.
std::vector<KeyHint> help_global_hints()
{
    return {
        KeyHint {"F1 / ?", "help"},
        KeyHint {"1-7", "switch tab"},
        KeyHint {":", "palette"},
        KeyHint {"q / Esc", "back"},
        KeyHint {"Ctrl-C", "quit"},
    };
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

/**
 * @brief Owns the interactive screen, its UI poster and the async caller with a
 * fixed member order that encodes the teardown invariant (03 review, T-120).
 *
 * Grouping the three in one struct makes the destruction order structural
 * rather than dependent on local-variable declaration order in @ref App::run:
 * members are destroyed in reverse declaration order, so @ref caller is torn
 * down first - joining its workers - before @ref poster and @ref screen, so a
 * completion in flight at shutdown can never post to a destroyed screen. DO NOT
 * REORDER THESE MEMBERS. The AsyncCaller-joins-before-poster semantics this
 * relies on are exercised headlessly by `04_app_teardown_test`; the full struct
 * cannot be driven in CI because its screen needs a real terminal.
 */
struct LoopRuntime
{
    ///< The FTXUI interactive screen; destroyed last.
    ftxui::ScreenInteractive screen;

    ///< Posts async completions onto @ref screen; destroyed after @ref caller.
    FtxuiPoster poster;

    ///< Runs commands off the UI thread; destroyed first (joins its workers).
    AsyncCaller caller;

    /**
     * @brief Wires the poster to @p s and the caller to @p client and poster.
     *
     * @param s The interactive screen, moved in.
     * @param client The command transport the caller runs; must outlive this.
     */
    LoopRuntime(ftxui::ScreenInteractive s, ICommandClient& client)
        : screen(std::move(s)), poster(screen), caller(client, poster)
    {
    }
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

    // Teardown ordering (UAF safety) is encoded by LoopRuntime's member order:
    // the caller is destroyed before the poster and screen (see its doc).
    LoopRuntime runtime(ScreenInteractive::Fullscreen(), m_client_);
    ScreenInteractive& screen = runtime.screen;
    AsyncCaller& caller = runtime.caller;
    screen.TrackMouse(false);  // mouse is an ADR 0003 non-goal (T-124)

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
        // The backend label is the nested `backend.metadata` field; the
        // persistence flag is a top-level bool (app::serializeSystemInfoJson).
        if (std::optional<std::string> label =
                backend_label_from_system_info(resp.result_json))
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
        const Theme& theme = theme_for(m_options_.theme);
        Element frame = render_frame(model, m_router_.active_tab(),
                                     tabs->Render(), hints, size.dimx,
                                     size.dimy, theme);
        if (m_help_visible_)
        {
            HelpModel help(help_global_hints(), hints);
            Element overlay = render_help_overlay(help, theme);
            // The frame root folds its own glyphs in mono; the overlay is
            // composited on top afterwards, so fold it too (idempotent).
            if (theme.ascii_only)
            {
                overlay = ascii_filter(std::move(overlay));
            }
            frame = dbox({std::move(frame), std::move(overlay)});
        }
        return frame;
    });

    root = CatchEvent(root, [&](const Event& event) -> bool {
        // While the help overlay is open it owns all input: Esc/F1/`?` close it,
        // everything else is swallowed so no key leaks to the screen beneath.
        if (m_help_visible_)
        {
            if (event == Event::Escape || event == Event::F1
                || event == Event::Character("?"))
            {
                m_help_visible_ = false;
            }
            return true;
        }
        // F1 opens help from anywhere, including while a text Input is focused
        // (checked before the text-input guard below); `?` stays typable.
        if (event == Event::F1)
        {
            m_help_visible_ = true;
            return true;
        }
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
        if (event == Event::Escape)
        {
            return m_router_.pop();
        }
        if (event.is_character() && event.character().size() == 1)
        {
            const char key = event.character()[0];
            if (key == '?')
            {
                m_help_visible_ = true;
                return true;
            }
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

    // FTXUI (v7.0.3) already restores termios + escapes on every exit path
    // including exceptions (its Loop destructor) and SIGINT/SIGTERM/SIGQUIT/
    // SIGHUP/SIGTSTP/crash signals; this restore is defense-in-depth for the
    // windows before Install / after Uninstall. The mode hook captures the
    // (cooked) line discipline so the SIGTERM handler can tcsetattr before
    // writing the escapes (T-123).
    FdTerminalSink sink(stdout_fd());
    TerminalRestore restore(sink);
    TtyModeRestore mode_restore(stdin_fd());
    ScopedSignalRestore signal_restore(restore, &mode_restore);

    run_with_restore(restore, [&] { screen.Loop(root); });
    return 0;
}

}  // namespace url_shortener::tui
