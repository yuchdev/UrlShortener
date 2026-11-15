#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/link_commands.hpp>

namespace url_shortener::commands
{

void register_link_commands(CommandRegistry& registry,
                            app::LinkCommandService& service)
{
    // Catalog order (3.1): create, read, mutate, lifecycle, preview, stats.
    // The registry sorts by name for listing, so this ordering is for human
    // readability of the registration site only; `registry.add` validates each
    // descriptor and rejects a duplicate name.
    registry.add(make_link_create_descriptor(service));
    registry.add(make_link_get_descriptor(service));
    registry.add(make_link_update_descriptor(service));
    registry.add(make_link_delete_descriptor(service));
    registry.add(make_link_enable_descriptor(service));
    registry.add(make_link_disable_descriptor(service));
    registry.add(make_link_restore_descriptor(service));
    registry.add(make_link_preview_descriptor(service));
    registry.add(make_link_stats_descriptor(service));
}

}  // namespace url_shortener::commands
