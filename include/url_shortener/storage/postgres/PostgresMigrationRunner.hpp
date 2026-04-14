#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "url_shortener/storage/errors/RepoError.hpp"

namespace soci {
class session;
}

/**
 * @brief Applies deterministic, versioned PostgreSQL migration assets.
 */
class PostgresMigrationRunner {
public:
    explicit PostgresMigrationRunner(std::filesystem::path migrations_dir);

    bool ApplyUp(soci::session& session, RepoError* error) const;
    bool ApplyDownToBase(soci::session& session, RepoError* error) const;

    std::vector<std::filesystem::path> OrderedUpMigrations() const;
    std::vector<std::filesystem::path> OrderedDownMigrations() const;

private:
    std::filesystem::path migrations_dir_;
};
