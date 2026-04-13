#pragma once

#include <map>
#include <memory>

#include "url_shortener/storage/sql/SqlMetadataRepository.hpp"

struct FakeSqlState {
    std::map<std::string, SqlRow> table;
    int bootstrap_calls = 0;
    int list_calls = 0;
};

struct FakeSqlSession final : ISqlSession {
    explicit FakeSqlSession(std::shared_ptr<FakeSqlState> state) : state(std::move(state)) {}
    std::shared_ptr<FakeSqlState> state;

    bool Bootstrap(const std::string&, RepoError* error) override { ++state->bootstrap_calls; if (error) *error = RepoError::none; return true; }
    bool InsertLink(const std::string&, const SqlRow& row, RepoError* error) override {
        auto key = row.short_code.value_or("");
        if (state->table.count(key)) { if (error) *error = RepoError::already_exists; return false; }
        state->table[key] = row;
        if (error) *error = RepoError::none;
        return true;
    }
    std::optional<SqlRow> SelectByShortCode(const std::string&, const std::string& short_code, RepoError* error) override {
        auto it = state->table.find(short_code);
        if (it == state->table.end()) { if (error) *error = RepoError::not_found; return std::nullopt; }
        if (error) *error = RepoError::none;
        return it->second;
    }
    bool UpdateLink(const std::string&, const SqlRow& row, RepoError* error) override {
        auto key = row.short_code.value_or("");
        auto it = state->table.find(key);
        if (it == state->table.end()) { if (error) *error = RepoError::not_found; return false; }
        state->table[key] = row;
        if (error) *error = RepoError::none;
        return true;
    }
    bool DeleteLink(const std::string&, const std::string& short_code, RepoError* error) override {
        state->table.erase(short_code);
        if (error) *error = RepoError::none;
        return true;
    }
    bool Exists(const std::string&, const std::string& short_code, bool* exists, RepoError* error) override {
        *exists = state->table.count(short_code) > 0;
        if (error) *error = RepoError::none;
        return true;
    }
    std::vector<SqlRow> List(const std::string&, const std::optional<std::string>&, bool, std::size_t, std::size_t, RepoError* error) override {
        ++state->list_calls;
        std::vector<SqlRow> out;
        for (const auto& [_, row] : state->table) out.push_back(row);
        if (error) *error = RepoError::none;
        return out;
    }
};

struct FakeFactory final : ISqlSessionFactory {
    std::shared_ptr<FakeSqlState> state = std::make_shared<FakeSqlState>();
    std::unique_ptr<ISqlSession> Create(RepoError*) const override {
        return std::make_unique<FakeSqlSession>(state);
    }
};
