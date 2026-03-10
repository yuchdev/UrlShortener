#include <url_shortener/cli/cli_parser.h>

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace {

bool parseBool(const std::string& value)
{
    if (value == "1" || value == "true" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    throw std::invalid_argument("Invalid boolean value: '" + value
        + "'. Use true/false, 1/0, on/off, or yes/no.");
}

uint16_t parsePort(uint32_t value)
{
    if (value == 0 || value > 65535) {
        throw std::out_of_range(
            "Port number " + std::to_string(value)
            + " is out of valid range [1, 65535].");
    }
    return static_cast<uint16_t>(value);
}

} // namespace

ParseResult CliParser::parse(int argc, char* argv[]) const
{
    ParseResult result;
    ServerConfig& cfg = result.config;

    // Read environment variable before CLI args so explicit CLI overrides it.
    if (const char* env = std::getenv("SHORTENER_BASE_DOMAIN");
        env != nullptr)
    {
        cfg.shortener_base_domain = env;
    }

    // ---- Option storage for values that need custom conversion ----
    std::string http_enabled_str;
    std::string http_redirect_to_https_str;
    uint32_t http_port_raw = cfg.http_port;
    uint32_t hsts_max_age_raw = 0;

    std::string tls_enabled_str;
    uint32_t https_port_raw = cfg.tls.port;
    std::string tls_session_tickets_str;
    std::string tls_session_cache_str;
    std::string tls_client_auth_str;

    std::string shortener_allow_private_targets_str;
    std::string analytics_enabled_str;

    // ---- Build options_description ----
    po::options_description general("General options");
    general.add_options()(
        "help,h",
        "Print this help message and exit."
    );

    po::options_description http_opts("HTTP options");
    http_opts.add_options()(
        "http-port",
        po::value<uint32_t>(&http_port_raw)->value_name("PORT"),
        "HTTP listening port (default: 8000)."
    )(
        "http-enabled",
        po::value<std::string>(&http_enabled_str)->value_name("BOOL"),
        "Enable HTTP listener (true/false, default: true)."
    )(
        "http-redirect-to-https",
        po::value<std::string>(&http_redirect_to_https_str)->value_name("BOOL"),
        "Redirect all HTTP requests to HTTPS (true/false, default: false)."
    )(
        "hsts-max-age",
        po::value<uint32_t>(&hsts_max_age_raw)->value_name("SECONDS"),
        "HSTS max-age in seconds (optional; omit to disable HSTS header)."
    );

    po::options_description tls_opts("TLS/HTTPS options");
    tls_opts.add_options()(
        "tls-enabled",
        po::value<std::string>(&tls_enabled_str)->value_name("BOOL"),
        "Enable TLS/HTTPS listener (true/false, default: false)."
    )(
        "https-port",
        po::value<uint32_t>(&https_port_raw)->value_name("PORT"),
        "HTTPS listening port (default: 443)."
    )(
        "tls-cert",
        po::value<std::string>(&cfg.tls.certificate_chain_file)->value_name("PATH"),
        "Path to PEM certificate chain file."
    )(
        "tls-key",
        po::value<std::string>(&cfg.tls.private_key_file)->value_name("PATH"),
        "Path to PEM private key file."
    )(
        "tls-key-passphrase",
        po::value<std::string>()->value_name("PASSPHRASE"),
        "Private key passphrase (optional)."
    )(
        "tls-ca-file",
        po::value<std::string>()->value_name("PATH"),
        "Path to CA certificate file (optional)."
    )(
        "tls-ca-path",
        po::value<std::string>()->value_name("PATH"),
        "Path to CA certificate directory (optional)."
    )(
        "tls-min-version",
        po::value<std::string>(&cfg.tls.min_version)
            ->value_name("VERSION")
            ->default_value(cfg.tls.min_version),
        "Minimum TLS version (default: TLS1.2)."
    )(
        "tls-cipher-suites",
        po::value<std::string>(&cfg.tls.cipher_suites)->value_name("SUITES"),
        "TLS 1.3 cipher suites (colon-separated)."
    )(
        "tls-ciphers",
        po::value<std::string>(&cfg.tls.ciphers)->value_name("CIPHERS"),
        "TLS 1.2 cipher list (OpenSSL format)."
    )(
        "tls-curves",
        po::value<std::string>(&cfg.tls.curves)
            ->value_name("CURVES")
            ->default_value(cfg.tls.curves),
        "Elliptic curves (default: X25519:P-256)."
    )(
        "tls-alpn",
        po::value<std::string>(&cfg.tls.alpn)
            ->value_name("ALPN")
            ->default_value(cfg.tls.alpn),
        "ALPN protocol (default: http/1.1)."
    )(
        "tls-session-tickets",
        po::value<std::string>(&tls_session_tickets_str)->value_name("BOOL"),
        "Enable TLS session tickets (true/false, default: false)."
    )(
        "tls-session-cache",
        po::value<std::string>(&tls_session_cache_str)->value_name("BOOL"),
        "Enable TLS session cache (true/false, default: true)."
    )(
        "tls-client-auth",
        po::value<std::string>(&tls_client_auth_str)->value_name("MODE"),
        "Client authentication mode: none, optional, or required "
        "(default: none)."
    );

    po::options_description shortener_opts("URL shortener options");
    shortener_opts.add_options()(
        "shortener-base-domain",
        po::value<std::string>(&cfg.shortener_base_domain)->value_name("URL"),
        "Base domain used in generated short URLs "
        "(default: http://localhost:8000; overridable via "
        "SHORTENER_BASE_DOMAIN env var)."
    )(
        "shortener-default-redirect-type",
        po::value<std::string>(&cfg.shortener_default_redirect_type)
            ->value_name("TYPE")
            ->default_value(cfg.shortener_default_redirect_type),
        "Default redirect type: temporary or permanent (default: temporary)."
    )(
        "shortener-generated-slug-length",
        po::value<uint32_t>(&cfg.shortener_generated_slug_length)
            ->value_name("N")
            ->default_value(cfg.shortener_generated_slug_length),
        "Length of auto-generated slugs (default: 7)."
    )(
        "shortener-allow-private-targets",
        po::value<std::string>(&shortener_allow_private_targets_str)
            ->value_name("BOOL"),
        "Allow private/intranet target URLs (true/false, default: false)."
    );

    po::options_description analytics_opts("Analytics options");
    analytics_opts.add_options()(
        "analytics-enabled",
        po::value<std::string>(&analytics_enabled_str)->value_name("BOOL"),
        "Enable analytics collection (true/false, default: true)."
    )(
        "analytics-queue-capacity",
        po::value<uint32_t>(&cfg.analytics_queue_capacity)
            ->value_name("N")
            ->default_value(cfg.analytics_queue_capacity),
        "In-memory analytics queue capacity (default: 1024)."
    )(
        "analytics-client-hash-salt",
        po::value<std::string>(&cfg.analytics_client_hash_salt)
            ->value_name("SALT")
            ->default_value(cfg.analytics_client_hash_salt),
        "HMAC salt for client ID hashing (default: dev-analytics-salt)."
    );

    po::options_description request_opts("Request limits");
    request_opts.add_options()(
        "request-id-max-length",
        po::value<uint32_t>(&cfg.request_id_max_length)
            ->value_name("N")
            ->default_value(cfg.request_id_max_length),
        "Maximum accepted X-Request-Id header length (default: 64)."
    )(
        "max-request-body-bytes",
        po::value<uint32_t>(&cfg.max_request_body_bytes)
            ->value_name("BYTES")
            ->default_value(cfg.max_request_body_bytes),
        "Maximum request body size in bytes (default: 65536)."
    )(
        "max-request-target-length",
        po::value<uint32_t>(&cfg.max_request_target_length)
            ->value_name("N")
            ->default_value(cfg.max_request_target_length),
        "Maximum request target (URL path) length (default: 2048)."
    );

    // Positional: bare integer argument sets http-port.
    po::options_description positional_hidden;
    positional_hidden.add_options()(
        "positional-port",
        po::value<uint32_t>(&http_port_raw)->value_name("PORT"),
        "Positional port shorthand (sets http-port)."
    );

    po::positional_options_description pos;
    pos.add("positional-port", 1);

    po::options_description all;
    all.add(general)
        .add(http_opts)
        .add(tls_opts)
        .add(shortener_opts)
        .add(analytics_opts)
        .add(request_opts)
        .add(positional_hidden);

    po::options_description visible;
    visible.add(general)
        .add(http_opts)
        .add(tls_opts)
        .add(shortener_opts)
        .add(analytics_opts)
        .add(request_opts);

    // ---- Parse ----
    po::variables_map vm;
    try {
        po::store(
            po::command_line_parser(argc, argv)
                .options(all)
                .positional(pos)
                .run(),
            vm
        );
    }
    catch (const po::error& e) {
        throw std::invalid_argument(e.what());
    }

    // ---- Handle --help before notify() ----
    if (vm.count("help")) {
        std::ostringstream oss;
        oss << "Usage: simple-http [OPTIONS] [PORT]\n\n" << visible;
        result.help_text = oss.str();
        result.help_requested = true;
        return result;
    }

    try {
        po::notify(vm);
    }
    catch (const po::error& e) {
        throw std::invalid_argument(e.what());
    }

    // ---- Convert and validate ----

    // HTTP port (from named --http-port or positional)
    if (vm.count("http-port") || vm.count("positional-port")) {
        cfg.http_port = parsePort(http_port_raw);
    }

    if (!http_enabled_str.empty()) {
        cfg.http_enabled = parseBool(http_enabled_str);
    }

    if (!http_redirect_to_https_str.empty()) {
        cfg.http_redirect_to_https = parseBool(http_redirect_to_https_str);
    }

    if (vm.count("hsts-max-age")) {
        cfg.hsts_max_age = hsts_max_age_raw;
    }

    // TLS
    if (!tls_enabled_str.empty()) {
        cfg.tls.enabled = parseBool(tls_enabled_str);
    }

    if (vm.count("https-port")) {
        cfg.tls.port = parsePort(https_port_raw);
    }

    if (vm.count("tls-key-passphrase")) {
        cfg.tls.private_key_passphrase = vm["tls-key-passphrase"].as<std::string>();
    }

    if (vm.count("tls-ca-file")) {
        cfg.tls.ca_file = vm["tls-ca-file"].as<std::string>();
    }

    if (vm.count("tls-ca-path")) {
        cfg.tls.ca_path = vm["tls-ca-path"].as<std::string>();
    }

    if (!tls_session_tickets_str.empty()) {
        cfg.tls.session_tickets = parseBool(tls_session_tickets_str);
    }

    if (!tls_session_cache_str.empty()) {
        cfg.tls.session_cache = parseBool(tls_session_cache_str);
    }

    if (!tls_client_auth_str.empty()) {
        cfg.tls.client_auth_mode = parseClientAuthMode(tls_client_auth_str);
    }

    if (!shortener_allow_private_targets_str.empty()) {
        cfg.shortener_allow_private_targets =
            parseBool(shortener_allow_private_targets_str);
    }

    if (!analytics_enabled_str.empty()) {
        cfg.analytics_enabled = parseBool(analytics_enabled_str);
    }

    return result;
}
