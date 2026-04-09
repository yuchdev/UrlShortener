"""Integration test 2: create custom slug then get by slug and by id."""
import unittest
from integration_common import ServerHarness, request


class CustomSlugGetByIdSlugTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 28102
        cls.server = ServerHarness(["--http-port", str(cls.port), "--tls-enabled", "false", "--shortener-base-domain", "http://sho.rt"])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_custom_slug_get_by_id_and_slug(self):
        status, payload, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/a","slug":"my-code"}', headers={"Content-Type": "application/json"})
        self.assertEqual(201, status)
        status, payload, _ = request(self.port, "GET", "/api/v1/links/my-code")
        self.assertEqual(200, status)
        link_id = payload.split('"id":"', 1)[1].split('"', 1)[0]
        status, payload, _ = request(self.port, "GET", f"/api/v1/links/id/{link_id}")
        self.assertEqual(200, status)
        self.assertIn('"slug":"my-code"', payload)


if __name__ == "__main__":
    unittest.main()
