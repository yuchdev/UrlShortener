"""Executable integration test with self-documenting scenario and failure triage.

This file validates a single API behavior end-to-end and includes method-level
Given/When/Then coverage, API surface, and what to check first on regression.
"""
import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class SoftDeleteManagementVisibilityTest(LinkManagementIntegrationBase):
    port = 38304

    def test_soft_delete_blocks_redirect_and_preview_shows_deleted(self):
        """
        [Integration][Link Management] Soft-delete blocks redirect and exposes deleted preview.
        
        Scenario:
            Given an active link.
            When DELETE /api/v1/links/{slug} is called.
            Then GET /{slug} returns link_deleted and preview returns status=deleted.
        
        API/Feature covered:
            - Stage 2 soft-delete lifecycle transition.
            - Preview representation for deleted links.
        
        If this breaks, first check:
            - deleted_at mutation.
            - Status precedence and preview response mapping.
        """
        status, _, _ = create_link(self.port, "lmdel01", "https://example.com/deleted")
        self.assertEqual(201, status)

        status, _, _ = request(self.port, "DELETE", "/api/v1/links/lmdel01")
        self.assertEqual(200, status)

        status, payload, _ = request(self.port, "GET", "/lmdel01")
        self.assertEqual(404, status)
        self.assertIn('"link_deleted"', payload)

        status, payload, _ = request(self.port, "GET", "/api/v1/links/lmdel01/preview")
        self.assertEqual(200, status)
        self.assertIn('"status":"deleted"', payload)


if __name__ == "__main__":
    unittest.main()
