#pragma once

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
 * @brief The TUI application shell: owns the FTXUI loop, the D1 frame and the
 * router, and hosts the registered screens (ADR 0003, D1/D11).
 *
 * @ref run builds the frame, kicks off `system.info` asynchronously, and runs
 * the FTXUI event loop until the user quits. Screens are added through
 * @ref register_screen before @ref run, so later subtasks plug in without the
 * shell knowing their concrete types. The app talks to the application only via
 * the injected @ref ICommandClient (contract C8).
 *
 * Teardown ordering (UAF safety, 03 security review): @ref run constructs the
 * `ScreenInteractive`, then the @ref FtxuiPoster over it, then the
 * @ref AsyncCaller over the poster, as locals in that order. They are therefore
 * destroyed in reverse - the caller first (joining its workers so no worker can
 * post afterwards), then the poster, then the screen - so a call still in
 * flight at quit can never post to a destroyed screen. Do not reorder those
 * locals.
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
     * @brief Registers a screen the frame can show.
     *
     * Must be called before @ref run. The app takes ownership; screens are
     * looked up by their @ref IScreen::id when the router switches tabs.
     *
     * @param screen The screen to host; must not be null.
     */
    void register_screen(std::unique_ptr<IScreen> screen);

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

    ///< The hosted screens, owned by this app; looked up by @ref IScreen::id.
    std::vector<std::unique_ptr<IScreen>> m_screens_;

    ///< `true` while a text input has focus, so the global hotkey handler
    ///< defers digits/`:`/`q` to the input. Wired by form/filter screens later.
    bool m_text_input_active_ = false;

    ///< The backend label shown in the title; `"connecting..."` until
    ///< `system.info` answers. Server data once set (rendered via the T-110
    ///< sink; see `frame_view.cpp`).
    std::string m_backend_label_ = "connecting...";

    ///< Whether the backend is persistent; drives the memory-store banner.
    ///< Defaults to persistent so the banner does not flash before the answer.
    bool m_persistent_ = true;
};

}  // namespace url_shortener::tui
