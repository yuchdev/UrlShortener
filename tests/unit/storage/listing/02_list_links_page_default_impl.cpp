#define BOOST_TEST_MODULE ListLinksPageDefaultImplTest
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/i_metadata_repository.hpp"
#include "url_shortener/storage/list_links_eval.hpp"
#include "url_shortener/storage/models/link_record.hpp"
#include "url_shortener/storage/models/list_links_query.hpp"

namespace
{

using Clock = std::chrono::system_clock;

LinkRecord MakeRecord(const std::string& short_code, int created_offset)
{
    LinkRecord r;
    r.id = short_code;
    r.short_code = short_code;
    r.target_url = "https://example.com/" + short_code;
    r.created_at = Clock::time_point {} + std::chrono::seconds(created_offset);
    r.updated_at = r.created_at;
    r.is_active = true;
    return r;
}

/**
 * @brief Minimal `IMetadataRepository` whose `ListLinks` returns a
 * caller-provided record set (ignoring the query's filter flags, like the
 * SQL backends ignore `include_deleted`) or a caller-provided error.
 *
 * It deliberately does NOT override `ListLinksPage`, exercising the
 * out-of-line default implementation under test.
 */
class FakeRepo final : public IMetadataRepository
{
public:
    std::vector<LinkRecord> records;
    RepoError list_error = RepoError::none;

    bool CreateLink(const CreateLinkRequest&,
                    const std::string&,
                    const std::chrono::system_clock::time_point&,
                    RepoError*) override
    {
        return true;
    }

    std::optional<LinkRecord> GetByShortCode(const std::string&,
                                             RepoError*) const override
    {
        return std::nullopt;
    }

    std::optional<LinkRecord> GetById(const std::string&,
                                      RepoError*) const override
    {
        return std::nullopt;
    }

    bool UpdateLink(const LinkRecord&, RepoError*) override { return true; }

    bool DeleteLink(const std::string&, RepoError*) override { return true; }

    std::vector<LinkRecord> ListLinks(const ListLinksQuery&,
                                      RepoError* error) const override
    {
        if (list_error != RepoError::none) {
            if (error != nullptr) {
                *error = list_error;
            }
            return {};
        }
        if (error != nullptr) {
            *error = RepoError::none;
        }
        return records;  // ignores filter flags on purpose
    }

    bool Exists(const std::string&, RepoError*) const override { return false; }
};

}  // namespace

// Fake returning N records -> page equals ApplyListQuery over the same set.
BOOST_AUTO_TEST_CASE(page_equals_apply_list_query)
{
    FakeRepo repo;
    repo.records.push_back(MakeRecord("aaa", 0));
    repo.records.push_back(MakeRecord("bbb", 1));
    repo.records.push_back(MakeRecord("ccc", 2));

    ListLinksQuery q;
    q.offset = 1;
    q.limit = 1;

    LinkPage page;
    RepoError err = RepoError::transient_failure;
    BOOST_REQUIRE(repo.ListLinksPage(q, &page, &err));
    BOOST_TEST(err == RepoError::none);

    LinkPage expected = ApplyListQuery(repo.records, q);
    BOOST_TEST(page.total == expected.total);
    BOOST_REQUIRE(page.items.size() == expected.items.size());
    BOOST_TEST(page.items.front().short_code
               == expected.items.front().short_code);
    BOOST_TEST(page.items.front().short_code == "bbb");
}

// Fake ignores include_deleted (like SQL); default impl re-filters.
BOOST_AUTO_TEST_CASE(deleted_records_filtered_even_if_backend_ignores_flag)
{
    FakeRepo repo;
    repo.records.push_back(MakeRecord("aaa", 0));
    LinkRecord deleted = MakeRecord("bbb", 1);
    deleted.deleted_at = Clock::time_point {} + std::chrono::seconds(50);
    repo.records.push_back(deleted);

    ListLinksQuery q;  // include_deleted defaults to false

    LinkPage page;
    BOOST_REQUIRE(repo.ListLinksPage(q, &page, nullptr));
    BOOST_TEST(page.total == 1u);
    BOOST_REQUIRE(page.items.size() == 1u);
    BOOST_TEST(page.items.front().short_code == "aaa");
}

// Fake returning exactly kListScanLimit -> false + permanent_failure.
BOOST_AUTO_TEST_CASE(scan_cap_overflow_is_permanent_failure)
{
    FakeRepo repo;
    repo.records.reserve(kListScanLimit);
    for (std::size_t i = 0; i < kListScanLimit; ++i) {
        repo.records.push_back(
            MakeRecord("c" + std::to_string(i), static_cast<int>(i)));
    }

    ListLinksQuery q;
    LinkPage page;
    RepoError err = RepoError::none;
    BOOST_TEST(!repo.ListLinksPage(q, &page, &err));
    BOOST_TEST(err == RepoError::permanent_failure);
}

// ListLinks error is propagated verbatim.
BOOST_AUTO_TEST_CASE(list_links_error_propagated)
{
    FakeRepo repo;
    repo.list_error = RepoError::transient_failure;

    ListLinksQuery q;
    LinkPage page;
    RepoError err = RepoError::none;
    BOOST_TEST(!repo.ListLinksPage(q, &page, &err));
    BOOST_TEST(err == RepoError::transient_failure);
}

// Null page pointer -> false + invalid_argument.
BOOST_AUTO_TEST_CASE(null_page_is_invalid_argument)
{
    FakeRepo repo;
    repo.records.push_back(MakeRecord("aaa", 0));

    ListLinksQuery q;
    RepoError err = RepoError::none;
    BOOST_TEST(!repo.ListLinksPage(q, nullptr, &err));
    BOOST_TEST(err == RepoError::invalid_argument);
}
