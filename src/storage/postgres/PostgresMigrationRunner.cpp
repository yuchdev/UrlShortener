#include "url_shortener/storage/postgres/PostgresMigrationRunner.hpp"

#include <algorithm>
#include <fstream>

#include <soci/session.h>
#include <soci/soci.h>

namespace {

std::string ReadFile(const std::filesystem::path& p)
{
    std::ifstream in(p);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::filesystem::path> OrderedBySuffix(const std::filesystem::path& dir, const std::string& suffix)
{
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::exists(dir)) {
        return out;
    }
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file()) {
            continue;
        }
        const auto name = e.path().filename().string();
        if (name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

PostgresMigrationRunner::PostgresMigrationRunner(std::filesystem::path migrations_dir)
    : migrations_dir_(std::move(migrations_dir))
{
}

std::vector<std::filesystem::path> PostgresMigrationRunner::OrderedUpMigrations() const
{
    return OrderedBySuffix(migrations_dir_, ".up.sql");
}

std::vector<std::filesystem::path> PostgresMigrationRunner::OrderedDownMigrations() const
{
    auto out = OrderedBySuffix(migrations_dir_, ".down.sql");
    std::reverse(out.begin(), out.end());
    return out;
}

bool PostgresMigrationRunner::ApplyUp(soci::session& session, RepoError* error) const
{
    try {
        for (const auto& migration : OrderedUpMigrations()) {
            session << ReadFile(migration);
        }
        if (error) *error = RepoError::none;
        return true;
    } catch (const std::exception&) {
        if (error) *error = RepoError::permanent_failure;
        return false;
    }
}

bool PostgresMigrationRunner::ApplyDownToBase(soci::session& session, RepoError* error) const
{
    try {
        for (const auto& migration : OrderedDownMigrations()) {
            session << ReadFile(migration);
        }
        if (error) *error = RepoError::none;
        return true;
    } catch (const std::exception&) {
        if (error) *error = RepoError::permanent_failure;
        return false;
    }
}
