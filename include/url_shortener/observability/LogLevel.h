#pragma once
#include <optional>
#include <string>

namespace url_shortener::observability {
enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal, Off };
std::string ToString(LogLevel level);
std::optional<LogLevel> ParseLogLevel(const std::string& value);
}
