#pragma once
#include <string>
#include <vector>
#include "url_shortener/observability/LoggingConfig.h"
namespace url_shortener::observability {
struct LogField { std::string key; std::string value; };
class Logger {
 public:
  Logger() = default; explicit Logger(std::string component): component_(std::move(component)) {}
  void trace(const std::string& msg, std::vector<LogField> f={}) const; void debug(const std::string& msg, std::vector<LogField> f={}) const;
  void info(const std::string& msg, std::vector<LogField> f={}) const; void warn(const std::string& msg, std::vector<LogField> f={}) const;
  void error(const std::string& msg, std::vector<LogField> f={}) const; void fatal(const std::string& msg, std::vector<LogField> f={}) const;
 private: std::string component_ = "app"; void log(LogLevel lvl, const std::string& msg, std::vector<LogField> f) const; };
}
