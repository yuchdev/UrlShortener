#pragma once
#include <string>
#include "url_shortener/observability/LoggingConfig.h"
namespace url_shortener::observability {
std::string SanitizeField(const std::string& value, const LoggingConfig& cfg, bool pii=false);
}
