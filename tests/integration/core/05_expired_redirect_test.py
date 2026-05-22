"""Integration test 5: expired link redirect returns 410 link_expired."""
import unittest
from integration_common import ServerHarness, request


class ExpiredRedirectTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 28105
        cls.server = ServerHarness(["--http-port", str(cls.port), "--tls-enabled", "false", "--shortener-base-domain", "http://sho.rt"])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_expired_link_redirect(self):
        """
        [Integration][Core] Expired links do not redirect.
        
        Scenario:
            Given POST /api/v1/links with past expires_at.
            When GET /{slug} is requested.
            Then response is 410 with link_expired code.
        
        API/Feature covered:
            - Redirect lifecycle gate for expired links.
        
        If this breaks, first check:
            - Expiry parsing/comparison logic.
            - Expired branch handling in redirect flow.
        """
        status, _, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/exp","slug":"exp001","expires_at":"2000-01-01T00:00:00Z"}', headers={"Content-Type": "application/json"})
        self.assertEqual(201, status)
        status, payload, _ = request(self.port, "GET", "/exp001")
        self.assertEqual(410, status)
        self.assertIn('"link_expired"', payload)


if __name__ == "__main__":
    unittest.main()
