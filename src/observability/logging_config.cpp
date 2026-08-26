#include "url_shortener/observability/LoggingConfig.h"
namespace url_shortener::observability {
bool ValidateLoggingConfig(const LoggingConfig& cfg, std::string& error){ if(cfg.sink==LogSink::File && cfg.file_path.empty()){error="log.file.path required when sink=file"; return false;} if(cfg.max_field_length==0){error="log.max_field_length must be >0"; return false;} return true; }
}
