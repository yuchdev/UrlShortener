#include "url_shortener/observability/logger_factory.hpp"

#include <memory>

#include "url_shortener/observability/file_sink.hpp"

namespace url_shortener::observability
{

///< Active logging configuration set by the most recent successful Initialize.
static LoggingConfig g_cfg {};

///< Open file sink when g_cfg.sink == LogSink::File; nullptr otherwise. Owned
///< here and closed on the next Initialize that replaces it.
static std::unique_ptr<FileSink> g_file_sink;

bool LoggerFactory::Initialize(const LoggingConfig& cfg, std::string* err)
{
    std::string e;
    if (!ValidateLoggingConfig(cfg, e)) {
        if (err) {
            *err = e;
        }
        return false;
    }

    // Open the file sink before mutating any global state so a failure leaves
    // the previous configuration and sink intact (fail-closed: never silently
    // redirect audit output to stderr).
    std::unique_ptr<FileSink> sink;
    if (cfg.sink == LogSink::File) {
        std::string open_err;
        sink = FileSink::Open(cfg.file_path, open_err);
        if (!sink) {
            if (err) {
                *err = open_err;  // value-free; never contains the path.
            }
            return false;
        }
    }

    g_file_sink = std::move(sink);  // closes any previously open sink.
    g_cfg = cfg;
    return true;
}

Logger LoggerFactory::Component(const std::string& c)
{
    return Logger(c);
}

const LoggingConfig& LoggerFactory::Config()
{
    return g_cfg;
}

FileSink* LoggerFactory::ActiveFileSink()
{
    return g_file_sink.get();
}

}  // namespace url_shortener::observability
