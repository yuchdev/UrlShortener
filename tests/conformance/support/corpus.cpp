/**
 * @file corpus.cpp
 * @brief Implementation of the corpus data model and loader (see corpus.hpp).
 */

#include "corpus.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

namespace conformance
{

namespace
{

/// Maps a corpus transport id string to its enumerator.
std::optional<Transport> transport_from_id(const std::string& id)
{
    static const std::array<std::pair<const char*, Transport>, 6> kIds = {{
        {"direct", Transport::Direct},
        {"rest_generic", Transport::RestGeneric},
        {"rest_resource", Transport::RestResource},
        {"cli_cmd", Transport::CliCmd},
        {"stdio", Transport::Stdio},
        {"tui", Transport::Tui},
    }};
    for (const auto& entry : kIds)
    {
        if (id == entry.first)
        {
            return entry.second;
        }
    }
    return std::nullopt;
}

/// Reads a required string member; records an error and returns false if absent.
bool require_string(const Json& object,
                    const char* key,
                    const std::string& where,
                    std::string* error,
                    std::string& out)
{
    const Json* value = object.find(key);
    if (value == nullptr || !value->is_string())
    {
        if (error != nullptr)
        {
            *error = where + ": missing or non-string '" + key + "'";
        }
        return false;
    }
    out = value->as_string();
    return true;
}

/// Parses an `expect` / `expect_accept` block into an @ref Expect.
bool parse_expect(const Json& block,
                  const std::string& where,
                  std::string* error,
                  Expect& out)
{
    if (!block.is_object())
    {
        if (error != nullptr)
        {
            *error = where + ": expect is not an object";
        }
        return false;
    }
    out.present = true;
    const Json* ok = block.find("ok");
    if (ok == nullptr || !ok->is_bool())
    {
        if (error != nullptr)
        {
            *error = where + ": expect.ok missing or non-bool";
        }
        return false;
    }
    out.ok = ok->as_bool();

    const Json* match = block.find("match");
    out.subset = match != nullptr && match->is_string() &&
                 match->as_string() == "subset";

    if (const Json* result = block.find("result"))
    {
        out.body = Expect::Body::Result;
        out.value = *result;
    }
    else if (const Json* job = block.find("job"))
    {
        out.body = Expect::Body::Job;
        out.value = *job;
    }
    else if (const Json* err = block.find("error"))
    {
        out.body = Expect::Body::Error;
        out.value = *err;
    }
    else
    {
        out.body = Expect::Body::None;
    }
    return true;
}

/// Parses the optional `await_job` block.
bool parse_await_job(const Json& block,
                     const std::string& where,
                     std::string* error,
                     AwaitJob& out)
{
    if (!block.is_object())
    {
        if (error != nullptr)
        {
            *error = where + ": await_job is not an object";
        }
        return false;
    }
    const Json* var = block.find("var");
    if (var == nullptr || !var->is_string())
    {
        if (error != nullptr)
        {
            *error = where + ": await_job.var missing";
        }
        return false;
    }
    out.var = var->as_string();
    if (const Json* timeout = block.find("timeout_ms"))
    {
        if (timeout->kind() == Json::Kind::Int)
        {
            out.timeout_ms = static_cast<int>(timeout->as_int());
        }
    }
    return true;
}

/// Parses the optional `rest_resource` block.
void parse_rest_resource(const Json& block, RestResource& out)
{
    if (const Json* method = block.find("method"); method && method->is_string())
    {
        out.method = method->as_string();
    }
    if (const Json* path = block.find("path"); path && path->is_string())
    {
        out.path = path->as_string();
    }
    if (const Json* query = block.find("query"))
    {
        out.query = *query;
    }
    if (const Json* body = block.find("body"))
    {
        out.body = *body;
    }
    if (const Json* status = block.find("status");
        status && status->kind() == Json::Kind::Int)
    {
        out.status = static_cast<int>(status->as_int());
    }
}

/// Parses one step object.
bool parse_step(const Json& block,
                const std::string& where,
                std::string* error,
                Step& out)
{
    if (!block.is_object())
    {
        if (error != nullptr)
        {
            *error = where + ": step is not an object";
        }
        return false;
    }
    if (const Json* label = block.find("label"); label && label->is_string())
    {
        out.label = label->as_string();
    }
    if (const Json* caller = block.find("caller"); caller && caller->is_string())
    {
        out.caller = caller->as_string();
    }
    const Json* request = block.find("request");
    const Json* raw = block.find("raw_request");
    if (request != nullptr)
    {
        out.request = *request;
    }
    if (raw != nullptr && raw->is_string())
    {
        out.raw_request = raw->as_string();
    }
    if (out.request.has_value() == out.raw_request.has_value())
    {
        if (error != nullptr)
        {
            *error = where + ": exactly one of request / raw_request required";
        }
        return false;
    }

    const Json* expect = block.find("expect");
    if (expect == nullptr)
    {
        if (error != nullptr)
        {
            *error = where + ": step missing expect";
        }
        return false;
    }
    if (!parse_expect(*expect, where, error, out.expect))
    {
        return false;
    }

    if (const Json* accept = block.find("expect_accept"))
    {
        if (!parse_expect(*accept, where, error, out.expect_accept))
        {
            return false;
        }
    }
    if (const Json* save = block.find("save"); save && save->is_object())
    {
        for (const Json::Member& member : save->members())
        {
            if (member.second.is_string())
            {
                out.save[member.first] = member.second.as_string();
            }
        }
    }
    if (const Json* await = block.find("await_job"))
    {
        AwaitJob job;
        if (!parse_await_job(*await, where, error, job))
        {
            return false;
        }
        out.await_job = std::move(job);
    }
    if (const Json* resource = block.find("rest_resource");
        resource && resource->is_object())
    {
        RestResource rr;
        parse_rest_resource(*resource, rr);
        out.rest_resource = std::move(rr);
    }
    if (const Json* status = block.find("http_status");
        status && status->kind() == Json::Kind::Int)
    {
        out.http_status = static_cast<int>(status->as_int());
    }
    if (const Json* exit = block.find("cli_exit");
        exit && exit->kind() == Json::Kind::Int)
    {
        out.cli_exit = static_cast<int>(exit->as_int());
    }
    return true;
}

/// Parses the optional `transports` block into a case's only/skip fields.
bool parse_transports(const Json& block,
                      const std::string& where,
                      std::string* error,
                      Case& out)
{
    if (!block.is_object())
    {
        return true;
    }
    if (const Json* only = block.find("only"); only && only->is_array())
    {
        for (const Json& element : only->items())
        {
            if (!element.is_string())
            {
                continue;
            }
            const std::optional<Transport> id =
                transport_from_id(element.as_string());
            if (!id.has_value())
            {
                if (error != nullptr)
                {
                    *error = where + ": unknown transport id in only";
                }
                return false;
            }
            out.only.insert(*id);
        }
    }
    if (const Json* skip = block.find("skip"); skip && skip->is_object())
    {
        for (const Json::Member& member : skip->members())
        {
            const std::optional<Transport> id = transport_from_id(member.first);
            if (!id.has_value())
            {
                if (error != nullptr)
                {
                    *error = where + ": unknown transport id in skip";
                }
                return false;
            }
            out.skip[*id] =
                member.second.is_string() ? member.second.as_string() : "";
        }
    }
    return true;
}

/// Parses one case object.
bool parse_case(const Json& block,
                const std::string& suite,
                std::string* error,
                Case& out)
{
    std::string where = suite + ": case";
    if (!block.is_object())
    {
        if (error != nullptr)
        {
            *error = where + " is not an object";
        }
        return false;
    }
    if (!require_string(block, "name", where, error, out.name))
    {
        return false;
    }
    where = suite + ": case '" + out.name + "'";
    if (!require_string(block, "doc", where, error, out.doc))
    {
        return false;
    }
    if (!require_string(block, "command", where, error, out.command))
    {
        return false;
    }
    out.suite = suite;
    if (const Json* caller = block.find("caller"); caller && caller->is_string())
    {
        out.caller = caller->as_string();
    }
    if (const Json* backend = block.find("backend");
        backend && backend->is_string())
    {
        out.backend = backend->as_string();
    }
    if (const Json* files = block.find("files"); files && files->is_object())
    {
        for (const Json::Member& member : files->members())
        {
            if (member.second.is_string())
            {
                out.files[member.first] = member.second.as_string();
            }
        }
    }
    if (const Json* transports = block.find("transports"))
    {
        if (!parse_transports(*transports, where, error, out))
        {
            return false;
        }
    }
    const Json* steps = block.find("steps");
    if (steps == nullptr || !steps->is_array() || steps->items().empty())
    {
        if (error != nullptr)
        {
            *error = where + ": steps must be a non-empty array";
        }
        return false;
    }
    for (std::size_t i = 0; i < steps->items().size(); ++i)
    {
        Step step;
        const std::string step_where =
            where + " step[" + std::to_string(i) + "]";
        if (!parse_step(steps->items()[i], step_where, error, step))
        {
            return false;
        }
        out.steps.push_back(std::move(step));
    }
    return true;
}

}  // namespace

std::string_view transport_id(Transport transport)
{
    switch (transport)
    {
        case Transport::Direct:
            return "direct";
        case Transport::RestGeneric:
            return "rest_generic";
        case Transport::RestResource:
            return "rest_resource";
        case Transport::CliCmd:
            return "cli_cmd";
        case Transport::Stdio:
            return "stdio";
        case Transport::Tui:
            return "tui";
    }
    return "unknown";
}

std::vector<Case> CorpusLoader::load_suite(const std::string& text,
                                           const std::string& source,
                                           std::string* error)
{
    std::string parse_error;
    const std::optional<Json> document = Json::parse(text, &parse_error);
    if (!document.has_value() || !document->is_object())
    {
        if (error != nullptr)
        {
            *error = source + ": invalid JSON (" + parse_error + ")";
        }
        return {};
    }
    const Json* version = document->find("corpus_version");
    if (version == nullptr || version->kind() != Json::Kind::Int ||
        version->as_int() != 1)
    {
        if (error != nullptr)
        {
            *error = source + ": corpus_version must be 1";
        }
        return {};
    }
    std::string suite;
    if (!require_string(*document, "suite", source, error, suite))
    {
        return {};
    }
    const Json* cases = document->find("cases");
    if (cases == nullptr || !cases->is_array())
    {
        if (error != nullptr)
        {
            *error = source + ": missing cases array";
        }
        return {};
    }
    std::vector<Case> result;
    for (const Json& block : cases->items())
    {
        Case parsed;
        if (!parse_case(block, suite, error, parsed))
        {
            return {};
        }
        result.push_back(std::move(parsed));
    }
    return result;
}

std::vector<Case> CorpusLoader::load_directory(
    const std::filesystem::path& dir, std::string* error)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            files.push_back(entry.path());
        }
    }
    if (ec)
    {
        if (error != nullptr)
        {
            *error = "cannot read corpus directory: " + dir.string();
        }
        return {};
    }
    std::sort(files.begin(), files.end());

    std::vector<Case> all;
    std::set<std::string> seen_names;
    for (const std::filesystem::path& file : files)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
        {
            if (error != nullptr)
            {
                *error = "cannot open corpus file: " + file.string();
            }
            return {};
        }
        const std::string text((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
        std::vector<Case> cases =
            load_suite(text, file.filename().string(), error);
        if (cases.empty() && error != nullptr && !error->empty())
        {
            return {};
        }
        for (Case& parsed : cases)
        {
            if (!seen_names.insert(parsed.name).second)
            {
                if (error != nullptr)
                {
                    *error = "duplicate case name across corpus: " + parsed.name;
                }
                return {};
            }
            all.push_back(std::move(parsed));
        }
    }
    return all;
}

}  // namespace conformance
