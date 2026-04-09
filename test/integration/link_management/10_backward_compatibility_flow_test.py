import unittest
from link_management_common import LinkManagementIntegrationBase, request


class BackwardCompatibilityFlowTest(LinkManagementIntegrationBase):
    port = 38310

    def test_legacy_create_read_redirect_flows(self):
        status, payload, _ = request(
            self.port,
            "POST",
            "/api/v1/short-urls",
            body='{"url":"https://example.com/legacy","code":"legacy01"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)
        self.assertIn('"slug":"legacy01"', payload)

        status, payload, _ = request(self.port, "GET", "/api/v1/short-urls/legacy01")
        self.assertEqual(200, status)
        self.assertIn('"slug":"legacy01"', payload)

        status, _, res = request(self.port, "GET", "/r/legacy01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/legacy", res.getheader("Location"))


if __name__ == "__main__":
    unittest.main()
