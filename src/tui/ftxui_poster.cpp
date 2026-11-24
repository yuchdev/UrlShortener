#include "url_shortener/tui/ftxui_poster.hpp"

#include <utility>

#include <ftxui/component/event.hpp>
#include <ftxui/component/task.hpp>

namespace url_shortener::tui
{

FtxuiPoster::FtxuiPoster(ftxui::ScreenInteractive& screen) : m_screen_(screen)
{
}

void FtxuiPoster::post(std::function<void()> task)
{
    // Post runs the closure on the FTXUI loop (the UI thread); the follow-up
    // Event::Custom guarantees one repaint after the task has mutated state,
    // so a response always lands on screen (async_caller spec step 5).
    m_screen_.Post(ftxui::Closure(std::move(task)));
    m_screen_.PostEvent(ftxui::Event::Custom);
}

}  // namespace url_shortener::tui
