import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class StatsEndpointStructureTest(LinkManagementIntegrationBase):
    port = 38308

    def test_stats_shape_and_counter_updates(self):
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
