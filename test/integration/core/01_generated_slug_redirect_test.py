"""Integration test 1: create with generated slug then resolve redirect."""
import unittest
from integration_common import ServerHarness, request


class GeneratedSlugRedirectTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 28101
        cls.server = ServerHarness(["--http-port", str(cls.port), "--tls-enabled", "false", "--shortener-base-domain", "http://sho.rt"])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_generated_slug_then_redirect(self):
        status, payload, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/path"}', headers={"Content-Type": "application/json"})
        self.assertEqual(201, status)
        slug = payload.split('"slug":"', 1)[1].split('"', 1)[0]
        status, _, res = request(self.port, "GET", f"/{slug}")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/path", res.getheader("Location"))


if __name__ == "__main__":
    unittest.main()
