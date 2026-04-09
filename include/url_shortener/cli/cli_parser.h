/**
 * @file cli_parser.h
 * @brief CLI argument parsing contract for url_shortener.
 */
#pragma once

#include <string>

#include <url_shortener/url_shortener.h>

/// Result returned by CliParser::parse().
///
/// When help_requested is true, help_text contains the formatted usage
/// message and the caller should print it and exit successfully.
struct ParseResult
{
    ServerConfig config;  ///< Parsed server configuration.
    bool help_requested = false;  ///< Indicates `--help` short-circuit.
    std::string help_text;  ///< Rendered help output.
};

/// Parses command-line arguments using Boost.ProgramOptions and converts
/// them into a ServerConfig.
///
/// Responsibilities:
///  - Defines and documents every supported CLI option in one place.
///  - Reads the SHORTENER_BASE_DOMAIN environment variable as a default.
///  - Validates argument values and produces clear error messages.
///  - Handles --help by populating ParseResult::help_text and setting
///    ParseResult::help_requested.
class CliParser
{
public:
    /// Parse argc/argv and return a ParseResult.
    ///
    /// Throws std::invalid_argument on unknown or invalid arguments.
    /// When --help is present, help_requested is set and config may be
    /// partially populated – callers should check help_requested first.
    ParseResult parse(int argc, char* argv[]) const;
};
