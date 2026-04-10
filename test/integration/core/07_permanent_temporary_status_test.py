"""Integration test 7: permanent redirects return 301 and temporary return 302."""
import unittest
from integration_common import ServerHarness, request


class PermanentTemporaryStatusTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 28107
        cls.server = ServerHarness(["--http-port", str(cls.port), "--tls-enabled", "false", "--shortener-base-domain", "http://sho.rt"])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_permanent_and_temporary_status_codes(self):
        """
        [Integration][Core] Redirect status follows redirect_type.
        
        Scenario:
            Given one permanent and one temporary link.
            When both slugs are resolved.
            Then permanent returns 301 and temporary returns 302.
        
        API/Feature covered:
            - redirect_type behavior on redirect endpoint.
        
        If this breaks, first check:
            - redirect_type parsing at create-time.
            - status-code mapping at redirect-time.
        """
        status, payload, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/p","slug":"perm01","redirect_type":"permanent"}', headers={"Content-Type": "application/json"})
        self.assertEqual(201, status)
        status, _, _ = request(self.port, "POST", "/api/v1/links", body='{"url":"https://example.com/t","slug":"temp01","redirect_type":"temporary"}', headers={"Content-Type": "application/json"})
        self.assertEqual(201, status)
        status, _, res = request(self.port, "GET", "/perm01")
        self.assertEqual(301, status)
        self.assertEqual("https://example.com/p", res.getheader("Location"))
        status, _, res = request(self.port, "GET", "/temp01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/t", res.getheader("Location"))


if __name__ == "__main__":
    unittest.main()
