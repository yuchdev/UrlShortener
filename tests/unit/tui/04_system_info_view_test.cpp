#define BOOST_TEST_MODULE TuiSystemInfoViewTest

#include <optional>
#include <string>

#include <boost/test/unit_test.hpp>

#include <url_shortener/app/system_info.hpp>
#include <url_shortener/tui/system_info_view.hpp>

using url_shortener::tui::backend_label_from_system_info;

namespace
{

// A SystemInfo whose backend.metadata label is distinctive, built so the test
// can serialize it with the real server serializer and read the label back.
url_shortener::app::SystemInfo make_info()
{
    url_shortener::app::SystemInfo info;
    info.version = "1.2.3";
    info.metadata = "sqlite (links.db)";
    info.cache = "redis";
    info.analytics = "sqlite";
    info.rate_limit = "redis";
    info.persistent = true;
    info.shortener.base_domain = "http://sho.rt";
    info.shortener.generated_slug_length = 7;
    info.shortener.default_redirect_type = "temporary";
    return info;
}

}  // namespace

// ---------------------------------------------------------------------------
// The label is read from the nested backend.metadata produced by the real
// serializer - not a (non-existent) top-level "backend" string.
// On failure first check: serializeSystemInfoJson changed the backend object
// shape, or backend_label_from_system_info stopped reading backend.metadata.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(reads_backend_metadata_from_real_serializer)
{
    const std::string json =
        url_shortener::app::serializeSystemInfoJson(make_info(), false);

    const std::optional<std::string> label =
        backend_label_from_system_info(json);
    BOOST_REQUIRE(label.has_value());
    BOOST_TEST(*label == "sqlite (links.db)");
}

// ---------------------------------------------------------------------------
// A top-level "backend" string (the earlier wrong assumption) is not a match:
// the helper only reads the nested object's metadata.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(nested_object_literal_is_read_flat_string_is_not)
{
    const std::string nested =
        R"({"backend":{"metadata":"postgres","cache":"none"},)"
        R"("persistent":false})";
    BOOST_TEST(*backend_label_from_system_info(nested) == "postgres");

    // backend as a flat string has no metadata member -> no label.
    const std::string flat = R"({"backend":"postgres","persistent":false})";
    BOOST_TEST(!backend_label_from_system_info(flat).has_value());
}

// ---------------------------------------------------------------------------
// Missing/malformed payloads yield nullopt rather than throwing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(missing_or_malformed_payload_yields_nullopt)
{
    BOOST_TEST(!backend_label_from_system_info("").has_value());
    BOOST_TEST(!backend_label_from_system_info("not json").has_value());
    BOOST_TEST(!backend_label_from_system_info("{}").has_value());
    // backend object present but without a metadata field.
    BOOST_TEST(!backend_label_from_system_info(R"({"backend":{"cache":"x"}})")
                    .has_value());
}
