#include <utility>

#include "url_shortener/commands/browse_commands.hpp"

#include "url_shortener/app/link_lister.hpp"
#include "url_shortener/app/system_info.hpp"
#include "url_shortener/app/table_source.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/system_commands.hpp"

namespace url_shortener::commands
{

void register_browse_commands(CommandRegistry& registry,
                              app::ILinkLister& lister,
                              app::TableCatalog& catalog,
                              app::SystemInfo info)
{
    registry.add(make_link_list_descriptor(lister));
    registry.add(make_db_tables_descriptor(catalog));
    registry.add(make_db_rows_descriptor(catalog));
    registry.add(make_system_info_descriptor(std::move(info)));
}

}  // namespace url_shortener::commands
