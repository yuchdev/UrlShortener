#include "url_shortener/observability/Logger.h"
#include "url_shortener/observability/LoggerFactory.h"
#include "url_shortener/observability/LogFields.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
namespace url_shortener::observability {
static bool enabled(LogLevel l, const LoggingConfig& c){return c.enabled && static_cast<int>(l)>=static_cast<int>(c.level) && c.level!=LogLevel::Off;}
void Logger::log(LogLevel lvl, const std::string& msg, std::vector<LogField> f) const { const auto& c=LoggerFactory::Config(); if(!enabled(lvl,c)) return; std::ostringstream ts; auto now=std::chrono::system_clock::now(); std::time_t t=std::chrono::system_clock::to_time_t(now); ts<<std::put_time(gmtime(&t),"%FT%TZ"); std::ostringstream out; out<<"ts="<<ts.str()<<" level="<<ToString(lvl)<<" component="<<component_<<" msg="<<msg; for(auto &e:f){ out<<' '<<e.key<<'='<<SanitizeField(e.value,c,e.key=="remote_addr"||e.key=="user_agent"); } out<<"\n"; if(c.sink==LogSink::Stdout) std::cout<<out.str(); else std::cerr<<out.str(); }
#define IMPL(name, lvl) void Logger::name(const std::string& m,std::vector<LogField> f) const { log(lvl,m,std::move(f)); }
IMPL(trace,LogLevel::Trace) IMPL(debug,LogLevel::Debug) IMPL(info,LogLevel::Info) IMPL(warn,LogLevel::Warn) IMPL(error,LogLevel::Error) IMPL(fatal,LogLevel::Fatal)
}
