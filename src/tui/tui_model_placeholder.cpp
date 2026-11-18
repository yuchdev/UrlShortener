#include <string>

namespace url_shortener::tui
{

/**
 * @brief Translation-unit placeholder that seeds the pure view-model library.
 *
 * `url_shortener_tui_model` is a STATIC library that must exist from subtask 01
 * (ADR 0003 rule 6 keeps view-models FTXUI-free so model tests run even where
 * the render library is troublesome), yet it has no view-models to compile
 * until later 06.0 subtasks append them to `TUI_MODEL_CPP_SOURCES`. CMake
 * rejects a STATIC library with no sources, so this file provides one trivial,
 * FTXUI-free translation unit. Later subtasks may remove it once real model
 * sources land.
 *
 * @return The stable identifier of the model library, for a smoke link check.
 */
std::string tui_model_library_id()
{
    return "url_shortener_tui_model";
}

}  // namespace url_shortener::tui
