#pragma once

#include <functional>

#include <ftxui/component/screen_interactive.hpp>

#include <url_shortener/tui/async_caller.hpp>

namespace url_shortener::tui
{

/**
 * @brief Binds @ref IUiPoster to an `ftxui::ScreenInteractive` so
 * @ref AsyncCaller completions run on the FTXUI UI thread.
 *
 * The only FTXUI-aware piece of the async hop (ADR 0003 rule 3): it forwards a
 * task to `ScreenInteractive::Post`, which the FTXUI event loop runs on its own
 * thread, and then posts an `Event::Custom` so a repaint is scheduled after the
 * task has updated the screen state.
 *
 * Thread-safety: @ref post may be called from any @ref AsyncCaller worker
 * thread; `ScreenInteractive::Post`/`PostEvent` are documented as thread-safe.
 * Lifetime: does not own the screen; the referenced `ftxui::ScreenInteractive`
 * must outlive this poster (and the @ref AsyncCaller that holds it).
 */
class FtxuiPoster final : public IUiPoster
{
public:
    /**
     * @brief Binds the poster to a live interactive screen.
     *
     * @param screen The screen whose event loop runs posted tasks; must outlive
     * this object. Not owned.
     */
    explicit FtxuiPoster(ftxui::ScreenInteractive& screen);

    /**
     * @brief Schedules @p task on the screen's event loop and requests a frame.
     *
     * Runs `screen.Post(task)` so the task executes on the UI thread, then
     * `screen.PostEvent(Event::Custom)` so exactly one repaint follows it.
     *
     * @param task The work to run on the UI thread; moved into the loop.
     */
    void post(std::function<void()> task) override;

private:
    ///< The interactive screen whose loop runs posted tasks; not owned, must
    ///< outlive this object.
    ftxui::ScreenInteractive& m_screen_;
};

}  // namespace url_shortener::tui
