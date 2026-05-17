#include "url_shortener/observability/LogLevel.h"
#include <algorithm>
namespace url_shortener::observability {
std::string ToString(LogLevel l){switch(l){case LogLevel::Trace:return"TRACE";case LogLevel::Debug:return"DEBUG";case LogLevel::Info:return"INFO";case LogLevel::Warn:return"WARN";case LogLevel::Error:return"ERROR";case LogLevel::Fatal:return"FATAL";case LogLevel::Off:return"OFF";}return"INFO";}
std::optional<LogLevel> ParseLogLevel(const std::string& v){std::string s=v; std::transform(s.begin(),s.end(),s.begin(),::tolower); if(s=="trace")return LogLevel::Trace; if(s=="debug")return LogLevel::Debug; if(s=="info")return LogLevel::Info; if(s=="warn"||s=="warning")return LogLevel::Warn; if(s=="error")return LogLevel::Error; if(s=="fatal")return LogLevel::Fatal; if(s=="off")return LogLevel::Off; return std::nullopt;}
}
