/**
 * @file transport_factory.cpp
 * @brief Composition factory that selects and constructs a stream transport
 * adapter by name (ADR 0002 section 4). Mirrors `BuildStorageAdapters` as the
 * single adapter-selection point; only `"stdio"` constructs a concrete
 * @ref url_shortener::transport::StdioAdapter (subtask 15).
 */

#include "url_shortener/composition/transport_factory.hpp"

#include <istream>
#include <ostream>

#include "url_shortener/transport/stdio_adapter.hpp"

namespace url_shortener::composition
{

std::unique_ptr<commands::ITransportAdapter>
make_transport_adapter(std::string_view name, const TransportOptions& options,
                       std::istream& in, std::ostream& out)
{
    if (name == "stdio") {
        return std::make_unique<transport::StdioAdapter>(in, out, options);
    }

    // Unknown (or empty) transport name: no adapter. Matching is case-sensitive.
    return nullptr;
}

} // namespace url_shortener::composition
