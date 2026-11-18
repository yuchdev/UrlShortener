#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/tui_options.hpp>

namespace url_shortener::tui
{

/**
 * @brief Builds one screen over the live @ref ScreenContext (subtask 19).
 *
 * A screen factory, not a ready-made screen: a real screen captures
 * `ScreenContext::caller&`, and the @ref AsyncCaller behind it exists only
 * inside @ref App::run (after the FTXUI screen is constructed). @ref App
 * therefore stores factories and instantiates them inside `run` once the caller
 * is alive, so every screen is built after - and destroyed before - the caller
 * (teardown invariant, threat model T-120). The factory must return a non-null
 * @ref IScreen and must not retain @p context beyond the returned screen's
 * lifetime. `tui_entry.cpp` (subtask 19) and later screen subtasks register one
 * factory per real screen; 08/11-17 slot in through this same signature.
 *
 * @param context The live screen context (caller, session, router, options);
 * owned by @ref App and valid for the returned screen's whole lifetime.
 * @return The constructed screen; never null.
 */
using ScreenFactory =
    std::function<std::unique_ptr<IScreen>(ScreenContext& context)>;

/**
 * @brief The TUI application shell: owns the FTXUI loop, the D1 frame and the
 * router, and hosts the registered screens (ADR 0003, D1/D11).
 *
 * @ref run builds the frame, kicks off `system.info` asynchronously, and runs
 * the FTXUI event loop until the user quits. Screen **factories** are added
 * through @ref register_screen before @ref run, so later subtasks plug in
 * without the shell knowing their concrete types; @ref run instantiates them
 * once the async caller exists (subtask 19). The app talks to the application
 * only via the injected @ref ICommandClient (contract C8).
 *
 * Teardown ordering (UAF safety, 03 security review): @ref run constructs the
 * `ScreenInteractive`, then the @ref FtxuiPoster over it, then the
 * @ref AsyncCaller over the poster, as locals in that order; the
 * @ref ScreenContext and the instantiated screens are locals declared **after**
 * that group. They are therefore destroyed in reverse - the screens first, then
 * the context, then the caller (joining its workers so no worker can post
 * afterwards), then the poster, then the screen. So a screen (which holds
 * `ScreenContext::caller&`) is always destroyed before the caller, and a call
 * still in flight at quit can never post to a destroyed screen. Do not reorder
 * those locals.
 */
class App
{
public:
    /**
     * @brief Builds the app over a command client and the process options.
     *
     * @param client The command transport the whole TUI runs on; must outlive
     * this app. Not owned (contract C8).
     * @param options Presentation/safety options (theme, read-only, --yes);
     * subtask 19 maps CLI flags onto them before constructing the app.
     */
    App(ICommandClient& client, TuiOptions options);

    /**
     * @brief Registers a screen factory the frame can instantiate.
     *
     * Must be called before @ref run. The factory is invoked once inside
     * @ref run, after the @ref AsyncCaller exists, with the live
     * @ref ScreenContext; the resulting screens are looked up by their
     * @ref IScreen::id when the router switches tabs and are destroyed before
     * the caller (teardown invariant). A factory must return a non-null screen.
     *
     * @param factory The screen factory to host; must not be empty.
     */
    void register_screen(ScreenFactory factory);

    /**
     * @brief Runs the frame and the FTXUI event loop until the user quits.
     *
     * Creates a fullscreen interactive screen with mouse tracking disabled
     * (ADR 0003 non-goal, T-124), wires the async hop, requests `system.info`
     * (showing "connecting..." until it answers; a failure is non-fatal), and
     * runs the loop wrapped so the terminal is restored on every exit path
     * including an exception or `SIGTERM` (T-123). Must be called on the main
     * thread.
     *
     * @return `0` on a clean exit.
     */
    int run();

private:
    ///< The command transport; not owned, must outlive this app.
    ICommandClient& m_client_;

    ///< Process-level presentation/safety options.
    TuiOptions m_options_;

    ///< Navigation state machine shared with every screen via its context.
    Router m_router_;

    ///< The registered screen factories, instantiated inside @ref run once the
    ///< @ref AsyncCaller exists (subtask 19). The resulting screens are looked
    ///< up by their @ref IScreen::id when the router switches tabs.
    std::vector<ScreenFactory> m_factories_;

    ///< `true` while the F1 help overlay is open; it then owns all input and is
    ///< drawn over the frame (subtask 05). Toggled by F1/`?`, closed by Esc.
    bool m_help_visible_ = false;

    ///< The backend label shown in the title; `"connecting..."` until
    ///< `system.info` answers. Server data once set (rendered via the T-110
    ///< sink; see `frame_view.cpp`).
    std::string m_backend_label_ = "connecting...";

    ///< Whether the backend is persistent; drives the memory-store banner.
    ///< Defaults to persistent so the banner does not flash before the answer.
    bool m_persistent_ = true;
};

}  // namespace url_shortener::tui
