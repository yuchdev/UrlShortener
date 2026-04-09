import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class EnableRedirectSuccessTest(LinkManagementIntegrationBase):
    port = 38303

    def test_reenable_then_redirect_succeeds(self):
        status, _, _ = create_link(self.port, "lmenb01", "https://example.com/enabled")
        self.assertEqual(201, status)

        status, _, _ = request(self.port, "POST", "/api/v1/links/lmenb01/disable")
        self.assertEqual(200, status)
        status, _, _ = request(self.port, "POST", "/api/v1/links/lmenb01/enable")
        self.assertEqual(200, status)

        status, _, res = request(self.port, "GET", "/lmenb01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/enabled", res.getheader("Location"))


if __name__ == "__main__":
    unittest.main()
