import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class CreateWithManagementFieldsTest(LinkManagementIntegrationBase):
    port = 38301

    def test_create_and_read_with_tags_metadata_campaign(self):
        status, payload, _ = create_link(
            self.port,
            "lmcrt01",
            "https://example.com/landing",
            '"tags":[" spring ","paid","paid"],"metadata":{"owner":"growth"},"campaign":{"name":"launch"}',
        )
        self.assertEqual(201, status)
        self.assertIn('"tags":["spring","paid"]', payload)
        self.assertIn('"metadata":{"owner":"growth"}', payload)
        self.assertIn('"campaign":{"name":"launch"}', payload)

        status, payload, _ = request(self.port, "GET", "/api/v1/links/lmcrt01")
        self.assertEqual(200, status)
        self.assertIn('"slug":"lmcrt01"', payload)


if __name__ == "__main__":
    unittest.main()
