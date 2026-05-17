#pragma once
#include <string>
#include "url_shortener/observability/LogLevel.h"

namespace url_shortener::observability {
enum class LogFormat { KeyValue, Json, Plain };
enum class LogSink { Stdout, Stderr, File };
struct LoggingConfig {
  bool enabled = true; LogLevel level = LogLevel::Info; LogFormat format = LogFormat::KeyValue;
  LogSink sink = LogSink::Stderr; std::string file_path{}; size_t file_max_size_mb = 100; size_t file_max_files = 5;
  LogLevel flush_level = LogLevel::Error; bool async = false; size_t async_queue_size = 8192;
  bool redact_pii = true; size_t max_field_length = 512;
};
bool ValidateLoggingConfig(const LoggingConfig& cfg, std::string& error);
}
