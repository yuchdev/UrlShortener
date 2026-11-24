/**
 * @file 60_transport_factory.cpp
 * @brief Unit tests for the transport seam (05.0/12): that
 * @ref url_shortener::commands::ITransportAdapter is implementable from the
 * header alone, and that
 * @ref url_shortener::composition::make_transport_adapter selects adapters by
 * exact, case-sensitive name and returns `nullptr` for anything it does not
 * know (including the as-yet-unimplemented `"stdio"`, flipped by subtask 15).
 */
#define BOOST_TEST_MODULE CommandsTransportFactoryTest
#include <memory>
#include <sstream>
#include <string_view>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/transport_adapter.hpp"
#include "url_shortener/composition/transport_factory.hpp"

using url_shortener::commands::CommandRegistry;
using url_shortener::commands::ITransportAdapter;
using url_shortener::composition::make_transport_adapter;
using url_shortener::composition::TransportOptions;

namespace
{

/**
 * @brief A minimal @ref ITransportAdapter written against the interface header
 * alone, proving the seam is implementable with no extra includes and that
 * @ref run is callable with a @ref CommandRegistry.
 */
class fake_adapter final : public ITransportAdapter
{
public:
    std::string_view name() const override
    {
        return "fake";
    }

    int run(CommandRegistry& registry) override
    {
        m_last_registry_ = &registry;
        ++m_run_count_;
        return 0;
    }

    /// How many times @ref run has been invoked.
    int run_count() const
    {
        return m_run_count_;
    }

    /// The registry passed to the most recent @ref run call, or `nullptr`.
    const CommandRegistry* last_registry() const
    {
        return m_last_registry_;
    }

private:
    int m_run_count_ = 0;
    const CommandRegistry* m_last_registry_ = nullptr;
};

} // namespace

BOOST_AUTO_TEST_CASE(fake_adapter_name_round_trips)
{
    fake_adapter adapter;
    BOOST_CHECK_EQUAL(adapter.name(), std::string_view("fake"));
}

BOOST_AUTO_TEST_CASE(fake_adapter_run_invoked_once_with_registry)
{
    fake_adapter adapter;
    CommandRegistry registry;

    const int code = adapter.run(registry);

    BOOST_CHECK_EQUAL(code, 0);
    BOOST_CHECK_EQUAL(adapter.run_count(), 1);
    BOOST_CHECK_EQUAL(adapter.last_registry(), &registry);
}

BOOST_AUTO_TEST_CASE(unknown_name_returns_null)
{
    std::istringstream in;
    std::ostringstream out;
    TransportOptions options;

    auto adapter = make_transport_adapter("bogus", options, in, out);

    BOOST_CHECK(adapter == nullptr);
}

BOOST_AUTO_TEST_CASE(empty_name_returns_null)
{
    std::istringstream in;
    std::ostringstream out;
    TransportOptions options;

    auto adapter = make_transport_adapter("", options, in, out);

    BOOST_CHECK(adapter == nullptr);
}

BOOST_AUTO_TEST_CASE(name_matching_is_case_sensitive)
{
    std::istringstream in;
    std::ostringstream out;
    TransportOptions options;

    BOOST_CHECK(make_transport_adapter("STDIO", options, in, out) == nullptr);
    BOOST_CHECK(make_transport_adapter("Stdio", options, in, out) == nullptr);
}

// Subtask 15: "stdio" now constructs a concrete StdioAdapter whose name() is
// "stdio". The factory remains the single adapter-selection point.
BOOST_AUTO_TEST_CASE(stdio_returns_adapter)
{
    std::istringstream in;
    std::ostringstream out;
    TransportOptions options;

    auto adapter = make_transport_adapter("stdio", options, in, out);

    BOOST_REQUIRE(adapter != nullptr);
    BOOST_CHECK_EQUAL(adapter->name(), std::string_view("stdio"));
}
