#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace url_shortener::tui
{

/**
 * @brief Reads the metadata-backend label from a `system.info` result payload.
 *
 * The `system.info` result nests the backend labels under a `backend` object
 * (`{"backend":{"metadata":...,"cache":...,"analytics":...,"rate_limit":...}}`,
 * see `app::serializeSystemInfoJson`), so the status bar's backend label is
 * `backend.metadata`, not a top-level `backend` string. This FTXUI-free helper
 * reads it through the sanctioned `json_view` scanner (contract C9); the caller
 * feeds the result into @ref FrameModel, which sanitizes and width-fits it
 * (T-110).
 *
 * @param result_json The `system.info` response `result` JSON text. May be
 * empty or malformed; no exception is thrown.
 * @return The decoded `backend.metadata` string when present; `std::nullopt`
 * when @p result_json is not a scannable object, has no `backend` object, or
 * that object has no string `metadata` field.
 */
std::optional<std::string>
backend_label_from_system_info(std::string_view result_json);

}  // namespace url_shortener::tui
