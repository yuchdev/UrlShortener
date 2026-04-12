#include "url_shortener/storage/sql/SqlMetadataRepository.hpp"

SqlMetadataRepository::SqlMetadataRepository(std::shared_ptr<ISqlSessionFactory> session_factory,
                                             std::shared_ptr<SqlDialect> dialect,
                                             std::shared_ptr<SqlMetadataRowMapper> mapper)
    : session_factory_(std::move(session_factory)),
      dialect_(std::move(dialect)),
      mapper_(std::move(mapper))
{
}

bool SqlMetadataRepository::EnsureBootstrapped(RepoError* error) const
{
    if (bootstrapped_) {
        if (error != nullptr) {
            *error = RepoError::none;
        }
        return true;
    }
    auto session = session_factory_->Create(error);
    if (!session) {
        return false;
    }
    if (!session->Bootstrap(dialect_->BootstrapSchemaSql(), error)) {
        return false;
    }
    bootstrapped_ = true;
    return true;
}

bool SqlMetadataRepository::CreateLink(const CreateLinkRequest& request,
                                       const std::string& id,
                                       const std::chrono::system_clock::time_point& now,
                                       RepoError* error)
{
    if (!request.isValid()) {
        if (error != nullptr) {
            *error = RepoError::invalid_argument;
        }
        return false;
    }
    if (!EnsureBootstrapped(error)) {
        return false;
    }
    auto session = session_factory_->Create(error);
    if (!session) {
        return false;
    }
    return session->InsertLink(dialect_->InsertSql(), mapper_->ToCreateRow(request, id, now), error);
}

std::optional<LinkRecord> SqlMetadataRepository::GetByShortCode(const std::string& short_code,
                                                                RepoError* error) const
{
    if (!EnsureBootstrapped(error)) {
        return std::nullopt;
    }
    auto session = session_factory_->Create(error);
    if (!session) {
        return std::nullopt;
    }
    auto row = session->SelectByShortCode(dialect_->SelectByShortCodeSql(), short_code, error);
    if (!row.has_value()) {
        return std::nullopt;
    }
    LinkRecord out;
    if (!mapper_->FromRow(*row, &out, error)) {
        return std::nullopt;
    }
    return out;
}

bool SqlMetadataRepository::UpdateLink(const LinkRecord& record, RepoError* error)
{
    if (!EnsureBootstrapped(error)) {
        return false;
    }
    auto session = session_factory_->Create(error);
    if (!session) {
        return false;
    }
    return session->UpdateLink(dialect_->UpdateSql(), mapper_->ToUpdateRow(record), error);
}

bool SqlMetadataRepository::DeleteLink(const std::string& short_code, RepoError* error)
{
    if (!EnsureBootstrapped(error)) {
        return false;
    }
    auto session = session_factory_->Create(error);
    if (!session) {
        return false;
    }
    return session->DeleteLink(dialect_->DeleteSql(), short_code, error);
}

std::vector<LinkRecord> SqlMetadataRepository::ListLinks(const ListLinksQuery& query, RepoError* error) const
{
    if (!EnsureBootstrapped(error)) {
        return {};
    }
    auto session = session_factory_->Create(error);
    if (!session) {
        return {};
    }
    auto rows = session->List(dialect_->ListSql(), query.owner, query.include_inactive, query.limit, query.offset, error);
    std::vector<LinkRecord> out;
    out.reserve(rows.size());
    for (const auto& row : rows) {
        LinkRecord mapped;
        if (!mapper_->FromRow(row, &mapped, error)) {
            return {};
        }
        out.push_back(std::move(mapped));
    }
    return out;
}

bool SqlMetadataRepository::Exists(const std::string& short_code, RepoError* error) const
{
    if (!EnsureBootstrapped(error)) {
        return false;
    }
    auto session = session_factory_->Create(error);
    if (!session) {
        return false;
    }
    bool exists = false;
    if (!session->Exists(dialect_->ExistsSql(), short_code, &exists, error)) {
        return false;
    }
    return exists;
}
