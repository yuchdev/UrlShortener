"""Integration test 3: duplicate custom slug returns 409 slug_conflict."""
import unittest
from integration_common import ServerHarness, request


class DuplicateSlugConflictTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 28103
        cls.server = ServerHarness(["--http-port", str(cls.port), "--tls-enabled", "false", "--shortener-base-domain", "http://sho.rt"])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_duplicate_custom_slug_conflict(self):
        status, _, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/a","slug":"dup-01"}', headers={"Content-Type": "application/json"})
        self.assertEqual(201, status)
        status, payload, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/b","slug":"dup-01"}', headers={"Content-Type": "application/json"})
        self.assertEqual(409, status)
        self.assertIn('"slug_conflict"', payload)


if __name__ == "__main__":
    unittest.main()
