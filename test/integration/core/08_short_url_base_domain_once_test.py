"""Integration test 8: short_url contains configured base domain exactly once."""
import unittest
from integration_common import ServerHarness, request


class ShortUrlBaseDomainOnceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 28108
        cls.server = ServerHarness(["--http-port", str(cls.port), "--tls-enabled", "false", "--shortener-base-domain", "http://sho.rt/"])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_short_url_base_domain_exactly_once(self):
        status, payload, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/x","slug":"base01"}', headers={"Content-Type": "application/json"})
        self.assertEqual(201, status)
        self.assertIn('"short_url":"http://sho.rt/base01"', payload)
        self.assertNotIn('http://sho.rt//base01', payload)


if __name__ == "__main__":
    unittest.main()
