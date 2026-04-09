import unittest
from link_management_common import LinkManagementIntegrationBase, request


class ReservedSlugConflictTest(LinkManagementIntegrationBase):
    port = 38309

    def test_reserved_slug_rejected_with_409(self):
        status, payload, _ = request(
            self.port,
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/r","slug":"API"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(409, status)
        self.assertIn('"reserved_slug"', payload)


if __name__ == "__main__":
    unittest.main()
