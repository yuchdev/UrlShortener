"""Integration test 6: missing slug returns 404."""
import unittest
from integration_common import ServerHarness, request


class MissingSlugNotFoundTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 28106
        cls.server = ServerHarness(["--http-port", str(cls.port), "--tls-enabled", "false", "--shortener-base-domain", "http://sho.rt"])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_missing_slug_returns_404(self):
        """
        [Integration][Core] Unknown slug returns not_found.
        
        Scenario:
            Given a slug that does not exist.
            When GET /does-not-exist is called.
            Then service returns 404 with not_found code.
        
        API/Feature covered:
            - Data-plane not-found behavior for redirect paths.
        
        If this breaks, first check:
            - Repository miss handling.
            - 404 error envelope generation.
        """
        status, payload, _ = request(self.port, "GET", "/does-not-exist")
        self.assertEqual(404, status)
        self.assertIn('"not_found"', payload)


if __name__ == "__main__":
    unittest.main()
