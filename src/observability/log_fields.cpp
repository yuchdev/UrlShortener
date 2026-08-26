#include "url_shortener/observability/LogFields.h"
namespace url_shortener::observability {
std::string SanitizeField(const std::string& value, const LoggingConfig& cfg, bool pii){ if(pii && cfg.redact_pii) return "[REDACTED]"; if(value.size()<=cfg.max_field_length) return value; return value.substr(0,cfg.max_field_length); }
}
