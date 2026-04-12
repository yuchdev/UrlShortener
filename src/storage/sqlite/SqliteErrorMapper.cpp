#include "url_shortener/storage/sqlite/SqliteErrorMapper.hpp"

#include <string>

RepoError SqliteErrorMapper::MapException(const std::exception& ex) const
{
    const std::string msg = ex.what();
    if (msg.find("UNIQUE") != std::string::npos || msg.find("unique") != std::string::npos) {
        return RepoError::already_exists;
    }
    if (msg.find("database is locked") != std::string::npos || msg.find("busy") != std::string::npos) {
        return RepoError::transient_failure;
    }
    if (msg.find("unable to open") != std::string::npos || msg.find("no such") != std::string::npos) {
        return RepoError::permanent_failure;
    }
    return RepoError::permanent_failure;
}
