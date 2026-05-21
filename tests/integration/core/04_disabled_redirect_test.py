"""Integration test 4: disabled link redirect returns 410 link_disabled."""
import unittest
from integration_common import ServerHarness, request


class DisabledRedirectTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 28104
        cls.server = ServerHarness(["--http-port", str(cls.port), "--tls-enabled", "false", "--shortener-base-domain", "http://sho.rt"])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_disabled_link_redirect(self):
        """
        [Integration][Core] Disabled links do not redirect.
        
        Scenario:
            Given POST /api/v1/links with enabled=false.
            When GET /{slug} is requested.
            Then response is 410 with link_disabled code.
        
        API/Feature covered:
            - Redirect lifecycle gate for disabled links.
        
        If this breaks, first check:
            - Enabled flag persistence.
            - Disabled branch handling in redirect path.
        """
        status, _, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/off","slug":"off001","enabled":false}', headers={"Content-Type": "application/json"})
        self.assertEqual(201, status)
        status, payload, _ = request(self.port, "GET", "/off001")
        self.assertEqual(410, status)
        self.assertIn('"link_disabled"', payload)


if __name__ == "__main__":
    unittest.main()
