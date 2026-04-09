import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class DisableRedirectBlockedTest(LinkManagementIntegrationBase):
    port = 38302

    def test_disable_then_redirect_disabled(self):
        status, _, _ = create_link(self.port, "lmdis01", "https://example.com/d")
        self.assertEqual(201, status)

        status, _, _ = request(self.port, "POST", "/api/v1/links/lmdis01/disable")
        self.assertEqual(200, status)

        status, payload, _ = request(self.port, "GET", "/lmdis01")
        self.assertEqual(410, status)
        self.assertIn('"link_disabled"', payload)


if __name__ == "__main__":
    unittest.main()
