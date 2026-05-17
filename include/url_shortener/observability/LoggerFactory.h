#pragma once
#include "url_shortener/observability/Logger.h"
namespace url_shortener::observability {
class LoggerFactory {
public:
  static bool Initialize(const LoggingConfig& cfg, std::string* error = nullptr);
  static Logger Component(const std::string& component);
  static const LoggingConfig& Config();
};
}
