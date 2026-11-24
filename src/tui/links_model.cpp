#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/links_model.hpp>

namespace url_shortener::tui
{

namespace
{

///< Placeholder for a missing, null or non-scalar cell value (D3).
constexpr char k_empty_cell[] = "-";

/**
 * @brief Resolves one (possibly dotted) key against a link element's JSON to a
 * display string, mirroring @ref rows_from_result's `cell_value`.
 *
 * @param element_json The link element's exact JSON object text.
 * @param key The field key or dotted path to resolve.
 * @return The decoded string / stringified number / `"true"`/`"false"`, or `-`
 * when any step is missing or the leaf is null or a container.
 */
std::string cell_value(const std::string& element_json, const std::string& key)
{
    std::string current = element_json;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = key.find('.', start);
        if (dot == std::string::npos) {
            break;
        }
        const std::string segment = key.substr(start, dot - start);
        std::optional<std::string> nested = raw_field(current, segment);
        if (!nested.has_value()) {
            return k_empty_cell;
        }
        current = std::move(*nested);
        start = dot + 1;
    }

    const std::string leaf = key.substr(start);
    if (std::optional<std::string> s = get_string(current, leaf)) {
        return *s;
    }
    if (std::optional<std::int64_t> n = get_int(current, leaf)) {
        return std::to_string(*n);
    }
    if (std::optional<bool> b = get_bool(current, leaf)) {
        return *b ? "true" : "false";
    }
    return k_empty_cell;
}

}  // namespace

LinksModel::LinksModel(CapabilityModel capability)
    : m_capability_(std::move(capability))
{
}

ActionAvailability LinksModel::availability(
    LinkAction action, const std::string& status_of_selected) const
{
    // Read actions are always available, even in a read-only session and even
    // without a selection where relevant.
    switch (action) {
        case LinkAction::detail:
        case LinkAction::preview:
        case LinkAction::stats:
            return ActionAvailability {true, std::string()};
        default:
            break;
    }

    // Every write action is refused when the capability denies writes (the one
    // decision point, subtask 18; defence in depth over the registry).
    CommandInfo write_probe;
    write_probe.permission = "write";
    const Capability cap = m_capability_.can_run(write_probe);
    if (!cap.allowed) {
        return ActionAvailability {false, cap.reason};
    }

    const bool deleted = status_of_selected == "deleted";
    const bool has_selection = !status_of_selected.empty();

    switch (action) {
        case LinkAction::create:
            // Creating a new link does not depend on the current selection.
            return ActionAvailability {true, std::string()};

        case LinkAction::edit:
            if (!has_selection) {
                return ActionAvailability {false, "select a link first"};
            }
            if (deleted) {
                return ActionAvailability {false, "restore the link first"};
            }
            return ActionAvailability {true, std::string()};

        case LinkAction::toggle:
            if (!has_selection) {
                return ActionAvailability {false, "select a link first"};
            }
            if (deleted) {
                return ActionAvailability {false, "restore the link first"};
            }
            return ActionAvailability {true, std::string()};

        case LinkAction::remove:
            if (!has_selection) {
                return ActionAvailability {false, "select a link first"};
            }
            if (deleted) {
                return ActionAvailability {false, "already deleted"};
            }
            return ActionAvailability {true, std::string()};

        case LinkAction::restore:
            if (!deleted) {
                return ActionAvailability {
                    false, "only deleted links can be restored"};
            }
            return ActionAvailability {true, std::string()};

        default:
            break;
    }
    return ActionAvailability {false, std::string()};
}

std::string LinksModel::toggle_command(
    const std::string& status_of_selected) const
{
    if (status_of_selected == "disabled") {
        return "link.enable";
    }
    if (status_of_selected == "active" || status_of_selected == "expired") {
        return "link.disable";
    }
    return std::string();  // deleted / unknown: toggle does not apply
}

std::vector<ColumnDef> LinksModel::columns() const
{
    // key, title, min_width, max_width, sortable, visible, right_align.
    // No column is sortable: Links disables the built-in sort (`s` means
    // "stats"), so a key that is not an allowed server `sort` value is never
    // emitted.
    return {
        ColumnDef {"slug", "SLUG", 4, 12, false, true, false},
        ColumnDef {"url", "TARGET URL", 8, 40, false, true, false},
        ColumnDef {"status", "STATUS", 6, 10, false, true, false},
        ColumnDef {"stats.total_redirects", "CLICKS", 4, 8, false, true, true},
        ColumnDef {"expires_at", "EXPIRES", 7, 10, false, true, false},
    };
}

std::vector<std::string> LinksModel::column_keys() const
{
    std::vector<std::string> keys;
    for (const ColumnDef& column : columns()) {
        keys.push_back(column.key);
    }
    return keys;
}

std::vector<std::string> LinksModel::row_from_link_json(
    const std::string& link_json) const
{
    std::vector<std::string> cells;
    for (const std::string& key : column_keys()) {
        cells.push_back(cell_value(link_json, key));
    }
    return cells;
}

}  // namespace url_shortener::tui
