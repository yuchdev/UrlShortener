"""Executable integration test with self-documenting scenario and failure triage.

This file validates a single API behavior end-to-end and includes method-level
Given/When/Then coverage, API surface, and what to check first on regression.
"""
import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class CreateWithManagementFieldsTest(LinkManagementIntegrationBase):
    port = 38301

    def test_create_and_read_with_tags_metadata_campaign(self):
        """
        [Integration][Link Management] Create/read with tags/metadata/campaign.
        
        Scenario:
            Given POST /api/v1/links with management fields.
            When link is created and read by GET /api/v1/links/{slug}.
            Then response includes normalized tags and provided metadata/campaign.
        
        API/Feature covered:
            - Stage 2 management-field create/read contract.
        
        If this breaks, first check:
            - Management payload validation/normalization.
            - Serializer fields for tags, metadata, and campaign.
        """
        status, payload, _ = create_link(
            self.port,
            "lmcrt01",
            "https://example.com/landing",
            '"tags":[" spring ","paid","paid"],"metadata":{"owner":"growth"},"campaign":{"name":"launch"}',
        )
        self.assertEqual(201, status)
        self.assertIn('"tags":["spring","paid"]', payload)
        self.assertIn('"metadata":{"owner":"growth"}', payload)
        self.assertIn('"campaign":{"name":"launch"}', payload)

        status, payload, _ = request(self.port, "GET", "/api/v1/links/lmcrt01")
        self.assertEqual(200, status)
        self.assertIn('"slug":"lmcrt01"', payload)


if __name__ == "__main__":
    unittest.main()
