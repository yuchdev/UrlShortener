/**
 * @file transport_factory.cpp
 * @brief Composition factory that selects and constructs a stream transport
 * adapter by name (ADR 0002 section 4). Mirrors `BuildStorageAdapters` as the
 * single adapter-selection point; only `"stdio"` is known, and its concrete
 * `StdioAdapter` branch is filled in by subtask 15.
 */

#include "url_shortener/composition/transport_factory.hpp"

#include <istream>
#include <ostream>

namespace url_shortener::composition
{

std::unique_ptr<commands::ITransportAdapter>
make_transport_adapter(std::string_view name, const TransportOptions& options,
                       std::istream& in, std::ostream& out)
{
    if (name == "stdio") {
        // Subtask 15 replaces this with:
        //   return std::make_unique<StdioAdapter>(options, in, out);
        // Until then the known transport has no implementation, so the factory
        // reports "not yet available" the same way it reports an unknown name:
        // a null adapter. The TDD test for subtask 15 flips this case.
        (void)options;
        (void)in;
        (void)out;
        return nullptr;
    }

    // Unknown (or empty) transport name: no adapter. Matching is case-sensitive.
    return nullptr;
}

} // namespace url_shortener::composition
