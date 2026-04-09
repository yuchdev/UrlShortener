import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class ExpiredRedirectBlockedTest(LinkManagementIntegrationBase):
    port = 38306

    def test_expired_link_redirect_blocked(self):
        status, _, _ = create_link(
            self.port,
            "lmexp01",
            "https://example.com/expired",
            '"expires_at":"2000-01-01T00:00:00Z"',
        )
        self.assertEqual(201, status)

        status, payload, _ = request(self.port, "GET", "/lmexp01")
        self.assertEqual(410, status)
        self.assertIn('"link_expired"', payload)


if __name__ == "__main__":
    unittest.main()
