#pragma once
#include <string>
#include "url_shortener/observability/logging_config.hpp"
namespace url_shortener::observability {
std::string SanitizeField(const std::string& value, const LoggingConfig& cfg, bool pii=false);
}
