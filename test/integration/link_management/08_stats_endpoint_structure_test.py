"""Executable integration test with self-documenting scenario and failure triage.

This file validates a single API behavior end-to-end and includes method-level
Given/When/Then coverage, API surface, and what to check first on regression.
"""
import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class StatsEndpointStructureTest(LinkManagementIntegrationBase):
    port = 38308

    def test_stats_shape_and_counter_updates(self):
        """
        [Integration][Link Management] Stats endpoint structure and counter updates.
        
        Scenario:
            Given three successful redirects for one slug.
            When GET /api/v1/links/{slug}/stats is requested.
            Then total_redirects and bucket fields reflect the successful count.
        
        API/Feature covered:
            - Stage 2 provisional stats endpoint contract.
        
        If this breaks, first check:
            - Counter increment on successful redirect.
            - Deterministic stats serializer fields.
        """
        status, _, _ = create_link(self.port, "lmstt01", "https://example.com/stats")
        self.assertEqual(201, status)

        for _ in range(3):
            status, _, _ = request(self.port, "GET", "/lmstt01")
            self.assertEqual(302, status)

        status, payload, _ = request(self.port, "GET", "/api/v1/links/lmstt01/stats")
        self.assertEqual(200, status)
        self.assertIn('"slug":"lmstt01"', payload)
        self.assertIn('"total_redirects":3', payload)
        self.assertIn('"redirects_24h":3', payload)
        self.assertIn('"redirects_7d":3', payload)
        self.assertIn('"last_accessed_at":', payload)
        self.assertIn('"created_at":', payload)


if __name__ == "__main__":
    unittest.main()
