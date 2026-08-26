#include "url_shortener/observability/LoggerFactory.h"
namespace url_shortener::observability { static LoggingConfig g_cfg{};
bool LoggerFactory::Initialize(const LoggingConfig& cfg, std::string* err){std::string e; if(!ValidateLoggingConfig(cfg,e)){if(err)*err=e;return false;} g_cfg=cfg; return true;}
Logger LoggerFactory::Component(const std::string& c){ return Logger(c); }
const LoggingConfig& LoggerFactory::Config(){ return g_cfg; }
}
