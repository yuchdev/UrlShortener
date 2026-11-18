#include "url_shortener/tui/theme_ftxui.hpp"

#include <memory>
#include <utility>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>

#include <url_shortener/tui/ascii.hpp>

namespace url_shortener::tui
{

namespace
{

/// Maps a model-layer @ref ColorId to a concrete FTXUI colour. @ref ColorId::none
/// becomes `Color::Default` (the terminal's own default), though
/// @ref to_decorator never emits a colour decorator for `none`.
ftxui::Color to_ftxui_color(ColorId id)
{
    switch (id)
    {
    case ColorId::red:
        return ftxui::Color::Red;
    case ColorId::green:
        return ftxui::Color::Green;
    case ColorId::yellow:
        return ftxui::Color::Yellow;
    case ColorId::blue:
        return ftxui::Color::Blue;
    case ColorId::cyan:
        return ftxui::Color::Cyan;
    case ColorId::grey:
        return ftxui::Color::GrayDark;
    case ColorId::none:
    default:
        return ftxui::Color::Default;
    }
}

/**
 * @brief A DOM node that renders its child, then folds box/block glyphs in the
 * child's rendered pixels to ASCII (the @ref ascii_filter implementation).
 *
 * Subclasses the public `ftxui::Node` directly (the library's internal
 * `NodeDecorator` lives under `src/` and is not an exported header in the
 * pinned FTXUI v7.0.3, so it is not used). It forwards layout to its single
 * child like a decorator and overrides `Render` to rewrite each pixel's
 * character through @ref transliterate_box_chars after the child has drawn.
 */
class AsciiFilterNode final : public ftxui::Node
{
public:
    /**
     * @brief Wraps @p child so its rendered glyphs are ASCII-folded.
     * @param child The element to render and post-process; must not be null.
     */
    explicit AsciiFilterNode(ftxui::Element child)
        : ftxui::Node(ftxui::Elements {std::move(child)})
    {
    }

    /// Adopts the child's layout requirement (decorator behaviour).
    void ComputeRequirement() override
    {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    /// Assigns the whole box to the single child (decorator behaviour).
    void SetBox(ftxui::Box box) override
    {
        ftxui::Node::SetBox(box);
        children_[0]->SetBox(box);
    }

    /// Renders the child, then transliterates every pixel in this node's box.
    void Render(ftxui::Screen& screen) override
    {
        children_[0]->Render(screen);

        const int x_min = box_.x_min < 0 ? 0 : box_.x_min;
        const int y_min = box_.y_min < 0 ? 0 : box_.y_min;
        const int x_max =
            box_.x_max >= screen.dimx() ? screen.dimx() - 1 : box_.x_max;
        const int y_max =
            box_.y_max >= screen.dimy() ? screen.dimy() - 1 : box_.y_max;

        for (int y = y_min; y <= y_max; ++y)
        {
            for (int x = x_min; x <= x_max; ++x)
            {
                std::string& ch = screen.PixelAt(x, y).character;
                ch = transliterate_box_chars(ch);
            }
        }
    }
};

}  // namespace

ftxui::Decorator to_decorator(const StyleSpec& s)
{
    using namespace ftxui;

    // Identity decorator; composed with each requested attribute/colour.
    Decorator d = nothing;
    if (s.bold)
    {
        d = d | bold;
    }
    if (s.dim)
    {
        d = d | dim;
    }
    if (s.inverted)
    {
        d = d | inverted;
    }
    if (s.fg != ColorId::none)
    {
        d = d | color(to_ftxui_color(s.fg));
    }
    if (s.bg != ColorId::none)
    {
        d = d | bgcolor(to_ftxui_color(s.bg));
    }
    return d;
}

ftxui::Element ascii_filter(ftxui::Element inner)
{
    return std::make_shared<AsciiFilterNode>(std::move(inner));
}

}  // namespace url_shortener::tui
