#pragma once

#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/theme.hpp>

namespace url_shortener::tui
{

/**
 * @brief Turns a model-layer @ref StyleSpec into an FTXUI decorator.
 *
 * The one place the FTXUI-free @ref ColorId / @ref StyleSpec vocabulary is
 * bound to concrete `ftxui::Color` values and attributes, so screens style text
 * by role (`to_decorator(theme.error)`) and never name an FTXUI colour (contract
 * K2, subtask constraint "screens never name colours"). The returned decorator
 * composes, in order, @ref StyleSpec::bold, @ref StyleSpec::dim,
 * @ref StyleSpec::inverted, foreground and background colour; a field left at
 * its default (false / @ref ColorId::none) contributes nothing, so an all-default
 * @ref StyleSpec yields an identity decorator that leaves the element unchanged.
 *
 * @param s The semantic style to realise.
 * @return An `ftxui::Decorator` applying @p s; an identity decorator when @p s
 * sets no colour and no attribute.
 */
ftxui::Decorator to_decorator(const StyleSpec& s);

/**
 * @brief Post-processes a rendered element so box-drawing and block glyphs
 * become ASCII (the @ref ThemeName::mono fallback).
 *
 * Wraps @p inner in a node that renders the child normally, then rewrites every
 * pixel in the child's box through @ref transliterate_box_chars, so borders and
 * bars drawn by FTXUI (`window`, `border`, `separator`, gauges) come out as
 * `+ - | #` on terminals that cannot show Unicode box-drawing (legacy consoles,
 * tmux with `TERM=screen`). Apply it once at the frame root when
 * @ref Theme::ascii_only is set; non-box text is untouched, so ordinary content
 * is unaffected. The rewrite is idempotent, so wrapping already-ASCII output is
 * harmless.
 *
 * @param inner The element to render and then ASCII-fold; must not be null.
 * @return A new element that renders @p inner and transliterates its glyphs;
 * never null.
 */
ftxui::Element ascii_filter(ftxui::Element inner);

}  // namespace url_shortener::tui
