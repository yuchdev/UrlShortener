#include "url_shortener/storage/sql/SqlMetadataRowMapper.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <sstream>

#include "url_shortener/storage/models/CreateLinkRequest.hpp"

namespace {

long long ToEpochSeconds(const std::chrono::system_clock::time_point& tp)
{
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point FromEpochSeconds(long long value)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(value));
}

std::string SerializePayload(const std::string& id, const std::map<std::string, std::string>& attrs)
{
    boost::property_tree::ptree root;
    root.put("id", id);
    boost::property_tree::ptree attrs_tree;
    for (const auto& [k, v] : attrs) {
        attrs_tree.put(k, v);
    }
    root.add_child("attributes", attrs_tree);
    std::ostringstream oss;
    boost::property_tree::write_json(oss, root, false);
    return oss.str();
}

bool DeserializePayload(const std::string& text, std::string* id, std::map<std::string, std::string>* attrs)
{
    boost::property_tree::ptree root;
    std::istringstream iss(text);
    boost::property_tree::read_json(iss, root);
    *id = root.get<std::string>("id");
    attrs->clear();
    const auto& attrs_tree = root.get_child("attributes", boost::property_tree::ptree{});
    for (const auto& kv : attrs_tree) {
        attrs->insert_or_assign(kv.first, kv.second.get_value<std::string>());
    }
    return true;
}

} // namespace

SqlRow SqlMetadataRowMapper::ToCreateRow(const CreateLinkRequest& request,
                                         const std::string& id,
                                         const std::chrono::system_clock::time_point& now) const
{
    SqlRow row;
    row.id = id;
    row.short_code = request.short_code;
    row.target_url = request.target_url;
    row.created_at = ToEpochSeconds(now);
    row.updated_at = ToEpochSeconds(now);
    row.expires_at = request.expires_at.has_value() ? std::optional<long long>(ToEpochSeconds(*request.expires_at)) : std::nullopt;
    row.is_active = request.is_active ? 1 : 0;
    row.owner_id = request.owner;
    row.attributes_json = SerializePayload(id, request.attributes);
    return row;
}

SqlRow SqlMetadataRowMapper::ToUpdateRow(const LinkRecord& record) const
{
    SqlRow row;
    row.id = record.id;
    row.short_code = record.short_code;
    row.target_url = record.target_url;
    row.created_at = ToEpochSeconds(record.created_at);
    row.updated_at = ToEpochSeconds(record.updated_at);
    row.expires_at = record.expires_at.has_value() ? std::optional<long long>(ToEpochSeconds(*record.expires_at)) : std::nullopt;
    row.is_active = record.is_active ? 1 : 0;
    row.owner_id = record.owner;
    row.attributes_json = SerializePayload(record.id, record.attributes);
    return row;
}

bool SqlMetadataRowMapper::FromRow(const SqlRow& row, LinkRecord* out, RepoError* error) const
{
    if (!row.short_code.has_value() || !row.target_url.has_value() || !row.created_at.has_value() ||
        !row.updated_at.has_value() || !row.is_active.has_value()) {
        if (error != nullptr) {
            *error = RepoError::permanent_failure;
        }
        return false;
    }

    out->short_code = *row.short_code;
    out->target_url = *row.target_url;
    out->created_at = FromEpochSeconds(*row.created_at);
    out->updated_at = FromEpochSeconds(*row.updated_at);
    out->expires_at = row.expires_at.has_value() ? std::optional<std::chrono::system_clock::time_point>(FromEpochSeconds(*row.expires_at)) : std::nullopt;
    out->owner = row.owner_id;
    out->is_active = (*row.is_active != 0);

    if (row.attributes_json.has_value()) {
        try {
            if (!DeserializePayload(*row.attributes_json, &out->id, &out->attributes)) {
                if (error != nullptr) {
                    *error = RepoError::permanent_failure;
                }
                return false;
            }
        } catch (...) {
            if (error != nullptr) {
                *error = RepoError::permanent_failure;
            }
            return false;
        }
    } else {
        out->id = row.id.value_or(row.short_code.value_or(""));
        out->attributes.clear();
    }

    if (error != nullptr) {
        *error = RepoError::none;
    }
    return true;
}
