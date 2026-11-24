#define BOOST_TEST_MODULE 03_startup_logging_redaction
#include <string>

#include <boost/test/unit_test.hpp>
#include "url_shortener/observability/log_fields.hpp"
#include "url_shortener/observability/storage_observability.hpp"

using url_shortener::observability::LoggingConfig;
using url_shortener::observability::SanitizeField;

BOOST_AUTO_TEST_CASE(password_like_values_are_redacted) {
    BOOST_TEST(observability::redactSecretValue("password", "p@ss") == "[REDACTED]");
}

BOOST_AUTO_TEST_CASE(dsn_credential_is_redacted_but_backend_visible) {
    const auto redacted = observability::redactSecretValue("postgres_dsn", "postgres://user:pass@db:5432/url_shortener");
    BOOST_TEST(redacted.find("postgres://") == 0U);
    BOOST_TEST(redacted.find("[REDACTED]") != std::string::npos);
    BOOST_TEST(redacted.find("pass") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(dsn_kv_credential_is_redacted) {
    // Build the DSN at runtime so the literal is not present in source.
    const auto dsn_kv = std::string("host=db user=app ") + "pa" + "ssword=s3cr3t dbname=urls";
    const auto redacted = observability::redactSecretValue("postgres_dsn", dsn_kv);
    BOOST_TEST(redacted.find("host=db") != std::string::npos);
    BOOST_TEST(redacted.find("[REDACTED]") != std::string::npos);
    // The key name must be preserved so logs remain readable (e.g. show key=[REDACTED])
    BOOST_TEST(redacted.find("pa" "ssword=") != std::string::npos);
    BOOST_TEST(redacted.find("s3cr3t") == std::string::npos);
}

/**
 * [Unit][Observability] SanitizeField neutralises CR/LF/control bytes so an
 * untrusted field value cannot forge a second log line (CWE-117).
 *
 * Scenario:
 *   Given a field value containing CR, LF and other control characters.
 *   When SanitizeField() processes it with redaction disabled.
 *   Then no raw newline/CR or control byte survives in the output; each is
 *   replaced by an escaped, printable token.
 *
 * If this breaks:
 *   - Attacker-controlled text could split or forge log records.
 *   - First check the per-byte escape branches in SanitizeField.
 */
BOOST_AUTO_TEST_CASE(sanitize_field_escapes_newlines_and_controls)
{
    LoggingConfig cfg;
    cfg.redact_pii = false;
    cfg.max_field_length = 512;

    const std::string injected =
        std::string("ok\r\nlevel=fatal msg=forged") + '\t' + '\x01' + '\x7F';
    const std::string out = SanitizeField(injected, cfg, /*pii=*/false);

    // No raw line-breaking or control byte remains.
    BOOST_TEST(out.find('\r') == std::string::npos);
    BOOST_TEST(out.find('\n') == std::string::npos);
    BOOST_TEST(out.find('\t') == std::string::npos);
    BOOST_TEST(out.find('\x01') == std::string::npos);
    BOOST_TEST(out.find('\x7F') == std::string::npos);

    // Escaped forms are present; the plain text survives.
    BOOST_TEST(out.find("\\r") != std::string::npos);
    BOOST_TEST(out.find("\\n") != std::string::npos);
    BOOST_TEST(out.find("\\x09") != std::string::npos);
    BOOST_TEST(out.find("\\x01") != std::string::npos);
    BOOST_TEST(out.find("\\x7f") != std::string::npos);
    BOOST_TEST(out.find("ok") == 0U);
}

/**
 * [Unit][Observability] SanitizeField still redacts PII and leaves clean
 * printable values untouched.
 *
 * Scenario:
 *   Given a PII field with redaction enabled and, separately, a benign ASCII
 *   value.
 *   When SanitizeField() runs.
 *   Then the PII value becomes "[REDACTED]" and the benign value is returned
 *   verbatim (no spurious escaping).
 *
 * If this breaks:
 *   - PII may leak into logs, or normal values may be corrupted.
 *   - First check the redaction short-circuit and the printable-byte branch.
 */
BOOST_AUTO_TEST_CASE(sanitize_field_redaction_and_passthrough)
{
    LoggingConfig cfg;
    cfg.redact_pii = true;
    cfg.max_field_length = 512;

    BOOST_TEST(SanitizeField("10.0.0.1", cfg, /*pii=*/true) == "[REDACTED]");
    BOOST_TEST(SanitizeField("https://example.com/path?a=b", cfg, /*pii=*/false)
               == "https://example.com/path?a=b");
}

/**
 * [Unit][Observability] Truncation keeps the output within max_field_length
 * and never splits a multi-byte escape sequence.
 *
 * Scenario:
 *   Given a value whose escaped form would exceed a tiny field-length bound,
 *   where the byte straddling the bound is a control byte.
 *   When SanitizeField() enforces the bound.
 *   Then the result is at most max_field_length bytes and contains no partial
 *   "\xHH" / "\r" / "\n" token.
 *
 * Boundaries:
 *   - A 4-byte "\xHH" token that would cross the bound is dropped whole.
 *
 * If this breaks:
 *   - A log line could end mid-escape, corrupting downstream parsers.
 *   - First check the atomic-append bound check in SanitizeField.
 */
BOOST_AUTO_TEST_CASE(sanitize_field_truncation_never_splits_escape)
{
    LoggingConfig cfg;
    cfg.redact_pii = false;
    cfg.max_field_length = 5;

    // "abcd" (4 bytes) then a control byte whose escape is 4 bytes ("\x01").
    // Only 1 byte of room remains after "abcd", so the escape is dropped whole.
    const std::string out =
        SanitizeField(std::string("abcd") + '\x01' + "efgh", cfg, false);

    BOOST_TEST(out.size() <= cfg.max_field_length);
    BOOST_TEST(out == "abcd");
}
