#include <cstdio>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#include "url_shortener/tui/app.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/app_navigation.hpp>
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

/// Default placeholder bodies for the eight D1 tabs. Analytics is a permanent
/// placeholder (ADR 0003 non-goal, provided by Milestone 0002). Every other tab
/// now ships a real screen registered by `tui_entry`, so its placeholder should
/// never be seen: it is a fallback that only renders if that factory was not
/// registered, hence the explicit "(bug)" wording.
const TabSlot k_tab_slots[] = {
    {ScreenId::dashboard, "Dashboard: factory not registered (bug)"},
    {ScreenId::links, "Links: factory not registered (bug)"},
    {ScreenId::tables, "Tables: factory not registered (bug)"},
    {ScreenId::import_export, "Import/Export: factory not registered (bug)"},
    {ScreenId::jobs, "Jobs: factory not registered (bug)"},
    {ScreenId::analytics, "Analytics: provided by Milestone 0002"},
    {ScreenId::settings, "Settings: factory not registered (bug)"},
    {ScreenId::palette, "Command palette: factory not registered (bug)"},
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
        : screen(std::move(s))
        , poster(screen)
        , caller(client, poster)
    {
    }
};

}  // namespace

App::App(ICommandClient& client, TuiOptions options)
    : m_client_(client)
    , m_options_(std::move(options))
{
}

void App::register_screen(ScreenFactory factory)
{
    m_factories_.push_back(std::move(factory));
}

int App::run()
{
    using namespace ftxui;

    // Teardown ordering (UAF safety) is encoded by LoopRuntime's member order:
    // the caller is destroyed before the poster and screen (see its doc). The
    // ScreenContext and the instantiated screens below are declared AFTER
    // `runtime`, so they are destroyed BEFORE the caller: a screen holds
    // ScreenContext::caller& and must never outlive it (subtask 19, T-120).
    LoopRuntime runtime(ScreenInteractive::Fullscreen(), m_client_);
    ScreenInteractive& screen = runtime.screen;
    AsyncCaller& caller = runtime.caller;
    screen.TrackMouse(false);  // mouse is an ADR 0003 non-goal (T-124)
    // FTXUI 7.0.3 defaults force_handle_ctrl_c_=true, which re-raises SIGINT on
    // Ctrl-C *regardless* of our Event::CtrlC -> screen.Exit() handler below,
    // so the process dies with 130 instead of exiting cleanly with 0 (BUG-1,
    // 06.0/20 smoke). Turn it off so Ctrl-C takes the clean Exit() path;
    // SIGTERM still uses FTXUI's own signal handling (which restores termios),
    // so a `kill -TERM` keeps working.
    screen.ForceHandleCtrlC(false);

    // The live context every screen reaches the application through (contract
    // C8). Declared after `runtime` (so it is destroyed before the caller it
    // references) and before `screens` (so it outlives them).
    ScreenContext context {caller, m_client_.session(), m_router_, m_options_};

    // Instantiate the registered real screens now the caller exists, then fill
    // a placeholder for any tab a real screen did not register, so the frame
    // and navigation work before subtasks 11-17 land (21 Settings wired in 19).
    std::vector<std::unique_ptr<IScreen>> screens;
    screens.reserve(m_factories_.size() + std::size(k_tab_slots));
    for (const ScreenFactory& factory : m_factories_) {
        screens.push_back(factory(context));
    }
    for (const TabSlot& slot : k_tab_slots) {
        bool present = false;
        for (const std::unique_ptr<IScreen>& s : screens) {
            if (s->id() == slot.id) {
                present = true;
                break;
            }
        }
        if (!present) {
            screens.push_back(make_placeholder_screen(slot.id, slot.message));
        }
    }

    std::vector<Component> components;
    components.reserve(screens.size());
    for (const std::unique_ptr<IScreen>& s : screens) {
        components.push_back(s->component());
    }
    int active_index = 0;
    Component tabs = Container::Tab(std::move(components), &active_index);

    // Load the backend label/persistent flag without blocking the first paint.
    RequestIdGenerator ids;
    commands::CommandRequest info;
    info.command = "system.info";
    info.id = ids.next();
    caller.submit(info,
                  [this](const commands::CommandResponse& resp)
                  {
                      if (!resp.ok) {
                          if (m_backend_label_ == "connecting...") {
                              m_backend_label_ = "unknown";
                          }
                          return;
                      }
                      // The backend label is the nested `backend.metadata`
                      // field; the persistence flag is a top-level bool
                      // (app::serializeSystemInfoJson).
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

    // The tab whose screen last received on_enter; drives the on_leave/on_enter
    // hand-off when the active tab changes (and the initial Dashboard enter).
    std::optional<ScreenId> entered_tab;

    Component root = Renderer(
        tabs,
        [&]
        {
            const Dimensions size = Terminal::Size();

            // Fire the on_leave/on_enter hand-off once per tab change (so a
            // data-backed screen refreshes when shown, subtask 19) and reset
            // the shared text-input focus so it reflects only the active screen
            // (BUG-2, 06.0/20 smoke). The render callback runs on the UI thread
            // and on_enter only submits async work, so this never blocks the
            // loop. The shared `sync_active_screen` is exercised headlessly by
            // `tui__20_app_navigation_test`.
            const int synced =
                sync_active_screen(screens, m_router_, context, entered_tab);
            active_index = synced >= 0 ? synced : 0;

            FrameModel model(m_client_.session(),
                             m_backend_label_,
                             m_persistent_,
                             std::string());
            std::vector<KeyHint> hints;
            if (active_index >= 0
                && static_cast<std::size_t>(active_index) < screens.size())
            {
                hints = screens[active_index]->hints();
            }
            const Theme& theme = theme_for(m_options_.theme);
            Element frame = render_frame(model,
                                         m_router_.active_tab(),
                                         tabs->Render(),
                                         hints,
                                         size.dimx,
                                         size.dimy,
                                         theme);
            if (m_help_visible_) {
                HelpModel help(help_global_hints(), hints);
                Element overlay = render_help_overlay(help, theme);
                // The frame root folds its own glyphs in mono; the overlay is
                // composited on top afterwards, so fold it too (idempotent).
                if (theme.ascii_only) {
                    overlay = ascii_filter(std::move(overlay));
                }
                frame = dbox({std::move(frame), std::move(overlay)});
            }
            return frame;
        });

    root = CatchEvent(root,
                      [&](const Event& event) -> bool
                      {
                          // The global hotkey logic (help overlay, the focused
                          // text-input guard, Ctrl-C quit, Esc/q back,
                          // digit/`:` tab switch) lives in the shared
                          // `handle_global_key` so it can be exercised
                          // headlessly by `tui__20_app_navigation_test`; Ctrl-C
                          // is reported as an exit request here so that free
                          // function needs no live screen.
                          const GlobalKeyOutcome outcome = handle_global_key(
                              event, m_router_, context, m_help_visible_);
                          if (outcome.exit_requested) {
                              screen.Exit();
                          }
                          return outcome.handled;
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
