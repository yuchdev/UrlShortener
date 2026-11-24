#define BOOST_TEST_MODULE MetadataContractGetByIdTest
#include <boost/test/unit_test.hpp>

#include "metadata_contract_common.hpp"

/**
 * [Contract][Metadata] GetById returns a created record, with
 * short_code/target_url matching, on every backend harness.
 */
// Each case uses its own slug/id: the Postgres harness shares one
// database across cases (no per-case reset), unlike the in-memory and
// SQLite harnesses.
BOOST_AUTO_TEST_CASE(contract_get_by_id_found)
{
    for (auto& h : MakeMetadataHarnesses()) {
        BOOST_REQUIRE(h.repo->CreateLink(
            BasicRequest("gbi001", "https://a"), "gbid-1", h.clock->now()));
        auto rec = h.repo->GetById("gbid-1");
        BOOST_REQUIRE_MESSAGE(rec.has_value(), h.name << ": id1 not found");
        BOOST_TEST(rec->id == "gbid-1");
        BOOST_TEST(rec->short_code == "gbi001");
        BOOST_TEST(rec->target_url == "https://a");
    }
}

/**
 * [Contract][Metadata] GetById on an unknown id returns nullopt and
 * leaves the error out-param at `none` (not-found is not a backend
 * error).
 */
BOOST_AUTO_TEST_CASE(contract_get_by_id_absent)
{
    for (auto& h : MakeMetadataHarnesses()) {
        RepoError error = RepoError::none;
        auto rec = h.repo->GetById("nope", &error);
        BOOST_TEST(!rec.has_value());
        BOOST_TEST((error == RepoError::none));
    }
}

/**
 * [Contract][Metadata] GetById does not filter by status: a record
 * deactivated via UpdateLink (is_active == false, the port-level proxy
 * for soft-deleted) is still returned.
 */
BOOST_AUTO_TEST_CASE(contract_get_by_id_inactive_returned)
{
    for (auto& h : MakeMetadataHarnesses()) {
        BOOST_REQUIRE(h.repo->CreateLink(
            BasicRequest("gbi002", "https://a"), "gbid-2", h.clock->now()));
        auto rec = h.repo->GetByShortCode("gbi002");
        BOOST_REQUIRE(rec.has_value());
        rec->is_active = false;
        BOOST_REQUIRE(h.repo->UpdateLink(*rec));

        auto fetched = h.repo->GetById("gbid-2");
        BOOST_REQUIRE_MESSAGE(fetched.has_value(),
                              h.name << ": inactive id1 not returned");
        BOOST_TEST(fetched->is_active == false);
    }
}

/**
 * [Contract][Metadata] GetById reflects a prior UpdateLink: the new
 * target_url is returned while the id is unchanged.
 */
BOOST_AUTO_TEST_CASE(contract_get_by_id_after_update)
{
    for (auto& h : MakeMetadataHarnesses()) {
        BOOST_REQUIRE(h.repo->CreateLink(
            BasicRequest("gbi003", "https://a"), "gbid-3", h.clock->now()));
        auto rec = h.repo->GetByShortCode("gbi003");
        BOOST_REQUIRE(rec.has_value());
        rec->target_url = "https://b";
        BOOST_REQUIRE(h.repo->UpdateLink(*rec));

        auto fetched = h.repo->GetById("gbid-3");
        BOOST_REQUIRE(fetched.has_value());
        BOOST_TEST(fetched->id == "gbid-3");
        BOOST_TEST(fetched->target_url == "https://b");
    }
}

/**
 * [Contract][Metadata] GetById returns nullopt after the record is
 * deleted by short_code.
 */
BOOST_AUTO_TEST_CASE(contract_get_by_id_after_delete)
{
    for (auto& h : MakeMetadataHarnesses()) {
        BOOST_REQUIRE(h.repo->CreateLink(
            BasicRequest("gbi004", "https://a"), "gbid-4", h.clock->now()));
        BOOST_REQUIRE(h.repo->DeleteLink("gbi004"));
        BOOST_TEST(!h.repo->GetById("gbid-4").has_value());
    }
}

/**
 * [Contract][Metadata] GetById on an empty id returns nullopt without an
 * error.
 */
BOOST_AUTO_TEST_CASE(contract_get_by_id_empty_id)
{
    for (auto& h : MakeMetadataHarnesses()) {
        RepoError error = RepoError::none;
        auto rec = h.repo->GetById("", &error);
        BOOST_TEST(!rec.has_value());
        BOOST_TEST((error == RepoError::none));
    }
}
