import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class SoftDeleteManagementVisibilityTest(LinkManagementIntegrationBase):
    port = 38304

    def test_soft_delete_blocks_redirect_and_preview_shows_deleted(self):
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
