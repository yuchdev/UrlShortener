#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

#include "url_shortener/observability/logger.hpp"

#include "url_shortener/core/utils.hpp"
#include "url_shortener/observability/file_sink.hpp"
#include "url_shortener/observability/log_fields.hpp"
#include "url_shortener/observability/logger_factory.hpp"

namespace url_shortener::observability
{

/**
 * @brief Whether a record at @p l should be emitted under config @p c.
 * @param l Severity of the record being considered.
 * @param c Active logging configuration.
 * @return `true` when logging is enabled, the level is not @c Off, and @p l is
 * at least the configured threshold; `false` otherwise.
 */
static bool enabled(LogLevel l, const LoggingConfig& c)
{
    return c.enabled && static_cast<int>(l) >= static_cast<int>(c.level)
        && c.level != LogLevel::Off;
}

void Logger::log(LogLevel lvl,
                 const std::string& msg,
                 std::vector<LogField> f) const
{
    const auto& c = LoggerFactory::Config();
    if (!enabled(lvl, c)) {
        return;
    }

    // Format the RFC3339 timestamp through the portable, thread-safe
    // gmtime_utc wrapper (plain std::gmtime shares a static std::tm buffer
    // across threads, which is itself a data race).
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
    gmtime_utc(&t, &tm);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%FT%TZ");

    std::ostringstream out;
    out << "ts=" << ts.str() << " level=" << ToString(lvl)
        << " component=" << component_ << " msg=" << msg;
    for (auto& e : f) {
        out << ' ' << e.key << '='
            << SanitizeField(
                   e.value, c, e.key == "remote_addr" || e.key == "user_agent");
    }
    out << "\n";
    const std::string line = out.str();

    // The FileSink has its own internal write lock, so only the shared
    // std::cout / std::cerr destinations need serializing here. A
    // function-local static std::mutex guards the single write, and the line
    // is emitted with the unformatted stream.write() so concurrent loggers
    // never race on the stream's formatting state (width/fill/precision).
    if (c.sink == LogSink::File) {
        if (FileSink* fs = LoggerFactory::ActiveFileSink()) {
            fs->Write(line);
        }
        return;
    }

    static std::mutex stream_mutex;
    std::ostream& stream = (c.sink == LogSink::Stdout) ? std::cout : std::cerr;
    std::lock_guard<std::mutex> lock(stream_mutex);
    stream.write(line.data(), static_cast<std::streamsize>(line.size()));
}

#define IMPL(name, lvl) \
    void Logger::name(const std::string& m, std::vector<LogField> f) const \
    { \
        log(lvl, m, std::move(f)); \
    }
IMPL(trace, LogLevel::Trace)
IMPL(debug, LogLevel::Debug)
IMPL(info, LogLevel::Info)
IMPL(warn, LogLevel::Warn)
IMPL(error, LogLevel::Error)
IMPL(fatal, LogLevel::Fatal)

}  // namespace url_shortener::observability
