#include "url_shortener/storage/postgres/PostgresErrorMapper.hpp"

#include <string>

RepoError PostgresErrorMapper::MapException(const std::exception& ex) const
{
    const std::string msg = ex.what();
    if (msg.find("23505") != std::string::npos || msg.find("unique") != std::string::npos) {
        return RepoError::already_exists;
    }
    if (msg.find("42P01") != std::string::npos || msg.find("undefined table") != std::string::npos) {
        return RepoError::permanent_failure;
    }
    if (msg.find("40001") != std::string::npos || msg.find("40P01") != std::string::npos ||
        msg.find("connection") != std::string::npos || msg.find("could not connect") != std::string::npos) {
        return RepoError::transient_failure;
    }
    if (msg.find("57014") != std::string::npos || msg.find("statement timeout") != std::string::npos ||
        msg.find("timeout") != std::string::npos || msg.find("canceling statement") != std::string::npos) {
        return RepoError::transient_failure;
    }
    if (msg.find("28P01") != std::string::npos || msg.find("password authentication failed") != std::string::npos) {
        return RepoError::permanent_failure;
    }
    return RepoError::permanent_failure;
}
