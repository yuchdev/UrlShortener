"""Integration test 9: Stage 2 idempotency, rapid toggles, and extension placeholders."""

import threading
import unittest

from integration_common import ServerHarness, request


class Stage2LifecycleNonFunctionalTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = 38209
        cls.server = ServerHarness([
            "--http-port",
            str(cls.port),
            "--tls-enabled",
            "false",
            "--shortener-base-domain",
            "http://sho.rt",
        ])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def test_idempotency_and_concurrency_toggles(self):
        status, _, _ = request(
            self.port,
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/concurrency","slug":"tog001"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)

        for _ in range(2):
            status, _, _ = request(self.port, "POST", "/api/v1/links/tog001/disable")
            self.assertEqual(200, status)
            status, payload, _ = request(self.port, "GET", "/tog001")
            self.assertEqual(410, status)
            self.assertIn('"link_disabled"', payload)

        for _ in range(2):
            status, _, _ = request(self.port, "POST", "/api/v1/links/tog001/enable")
            self.assertEqual(200, status)

        status, _, res = request(self.port, "GET", "/tog001")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/concurrency", res.getheader("Location"))

        outcomes = []

        def set_disable():
            outcomes.append(request(self.port, "POST", "/api/v1/links/tog001/disable")[0])

        def set_enable():
            outcomes.append(request(self.port, "POST", "/api/v1/links/tog001/enable")[0])

        threads = []
        for _ in range(20):
            threads.append(threading.Thread(target=set_disable))
            threads.append(threading.Thread(target=set_enable))
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        self.assertTrue(all(code == 200 for code in outcomes))

        status, payload, _ = request(self.port, "GET", "/api/v1/links/tog001/preview")
        self.assertEqual(200, status)
        self.assertTrue('"status":"active"' in payload or '"status":"disabled"' in payload)

    def test_delete_restore_idempotency_and_feature_placeholders(self):
        status, _, _ = request(
            self.port,
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/delete","slug":"del001"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)

        for _ in range(2):
            status, _, _ = request(self.port, "DELETE", "/api/v1/links/del001")
            self.assertEqual(200, status)

        for _ in range(2):
            status, _, _ = request(self.port, "POST", "/api/v1/links/del001/restore")
            self.assertEqual(200, status)

        status, _, res = request(self.port, "GET", "/del001")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/delete", res.getheader("Location"))

        status, payload, _ = request(self.port, "GET", "/api/v1/links/del001/qr")
        self.assertEqual(501, status)
        self.assertIn('"feature_not_enabled"', payload)

        status, payload, _ = request(self.port, "GET", "/api/v1/links/del001/routing")
        self.assertEqual(501, status)
        self.assertIn('"feature_not_enabled"', payload)

    def test_placeholder_endpoints_return_404_for_missing_slug(self):
        status, payload, _ = request(self.port, "GET", "/api/v1/links/nonexistent/qr")
        self.assertEqual(404, status)
        self.assertIn('"not_found"', payload)

        status, payload, _ = request(self.port, "GET", "/api/v1/links/nonexistent/routing")
        self.assertEqual(404, status)
        self.assertIn('"not_found"', payload)


if __name__ == "__main__":
    unittest.main()
