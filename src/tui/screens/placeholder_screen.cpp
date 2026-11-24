#include "url_shortener/tui/screens/placeholder_screen.hpp"

#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>

namespace url_shortener::tui
{

ftxui::Element placeholder_body(const std::string& message)
{
    using namespace ftxui;
    return hbox({filler(),
                 vbox({filler(), text(message), filler()}),
                 filler()});
}

namespace
{

/**
 * @brief An @ref IScreen that only renders a centered placeholder message.
 */
class PlaceholderScreen final : public IScreen
{
public:
    /**
     * @brief Builds the placeholder for @p id showing @p message.
     *
     * @param id The screen this stands in for.
     * @param message The body text; moved in.
     */
    PlaceholderScreen(ScreenId id, std::string message)
        : m_id_(id), m_message_(std::move(message))
    {
        m_component_ = ftxui::Renderer(
            [this] { return placeholder_body(m_message_); });
    }

    ScreenId id() const override { return m_id_; }

    ftxui::Component component() override { return m_component_; }

    std::vector<KeyHint> hints() const override { return {}; }

private:
    ///< The screen this placeholder represents.
    ScreenId m_id_;

    ///< The centered message rendered in the body.
    std::string m_message_;

    ///< The cached FTXUI component rendering @ref m_message_.
    ftxui::Component m_component_;
};

}  // namespace

std::unique_ptr<IScreen> make_placeholder_screen(ScreenId id,
                                                 std::string message)
{
    return std::make_unique<PlaceholderScreen>(id, std::move(message));
}

}  // namespace url_shortener::tui
