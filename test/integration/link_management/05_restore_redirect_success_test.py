import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class RestoreRedirectSuccessTest(LinkManagementIntegrationBase):
    port = 38305

    def test_restore_soft_deleted_link(self):
        status, _, _ = create_link(self.port, "lmres01", "https://example.com/restored")
        self.assertEqual(201, status)

        status, _, _ = request(self.port, "DELETE", "/api/v1/links/lmres01")
        self.assertEqual(200, status)
        status, _, _ = request(self.port, "POST", "/api/v1/links/lmres01/restore")
        self.assertEqual(200, status)

        status, _, res = request(self.port, "GET", "/lmres01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/restored", res.getheader("Location"))


if __name__ == "__main__":
    unittest.main()
