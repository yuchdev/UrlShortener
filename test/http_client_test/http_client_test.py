"""HTTP/HTTPS integration tests for URL shortener service."""

import http.client
import os
import shutil
import ssl
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request


def find_server_path():
    """Resolve server executable path from env or common build output locations."""
    env_bin = os.environ.get("SIMPLE_HTTP_BIN")
    if env_bin and os.path.exists(env_bin):
        return env_bin

    candidates = [
        os.path.abspath("../../cmake-build-debug/simple-http"),
        os.path.abspath("../../cmake-build-release/simple-http"),
        os.path.abspath("../../build/simple-http"),
    ]
    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate
    raise RuntimeError("Server executable not found")


class ServerHarness:
    """Small process lifecycle helper used by integration tests."""
    def __init__(self, args):
        """Store command-line arguments for spawned server process."""
        self.args = args
        self.process = None

    def start(self):
        """Start server subprocess and fail fast when startup fails."""
        self.process = subprocess.Popen(
            [find_server_path()] + self.args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        time.sleep(1.5)
        if self.process.poll() is not None:
            out, err = self.process.communicate(timeout=2)
            raise RuntimeError(f"Server failed to start\nstdout={out}\nstderr={err}")

    def stop(self):
        """Terminate server subprocess if it is still running."""
        if self.process and self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)


class TestHttpAndHttps(unittest.TestCase):
    """TLS and HTTP->HTTPS redirect integration checks."""
    @classmethod
    def setUpClass(cls):
        cls.temp_dir = tempfile.mkdtemp(prefix="https-test-")
        cls.cert = os.path.join(cls.temp_dir, "server.crt")
        cls.key = os.path.join(cls.temp_dir, "server.key")
        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "3",
                "-nodes", "-subj", "/CN=localhost", "-keyout", cls.key, "-out", cls.cert,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        os.chmod(cls.key, 0o600)

        cls.server = ServerHarness([
            "--http-port", "18080",
            "--tls-enabled", "true",
            "--https-port", "18443",
            "--tls-cert", cls.cert,
            "--tls-key", cls.key,
            "--http-redirect-to-https", "true",
            "--hsts-max-age", "300",
        ])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()
        shutil.rmtree(cls.temp_dir, ignore_errors=True)

    def test_https_request_success(self):
        """Validate successful HTTPS read/write flow and security response headers."""
        context = ssl.create_default_context(cafile=self.cert)
        conn = http.client.HTTPSConnection("localhost", 18443, context=context)
        conn.request("POST", "/foo", body="hello", headers={"Content-Type": "text/plain"})
        res = conn.getresponse()
        self.assertEqual(200, res.status)
        conn.close()

        conn = http.client.HTTPSConnection("localhost", 18443, context=context)
        conn.request("GET", "/foo")
        res = conn.getresponse()
        body = res.read().decode("utf-8")
        self.assertEqual(200, res.status)
        self.assertIn("max-age=300", res.getheader("Strict-Transport-Security"))
        self.assertEqual("nosniff", res.getheader("X-Content-Type-Options"))
        self.assertIn("hello", body)
        conn.close()

    def test_reject_tls_1_1_client(self):
        """Ensure TLS 1.1-only clients are rejected by server minimum TLS policy."""
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.minimum_version = ssl.TLSVersion.TLSv1_1
        context.maximum_version = ssl.TLSVersion.TLSv1_1
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        with self.assertRaises((ssl.SSLError, urllib.error.URLError)):
            with urllib.request.urlopen("https://localhost:18443", context=context, timeout=3):
                pass

    def test_http_redirects_to_https(self):
        """Ensure plaintext HTTP requests are redirected to HTTPS when enabled."""
        conn = http.client.HTTPConnection("localhost", 18080)
        conn.request("GET", "/foo", headers={"Host": "localhost:18080"})
        res = conn.getresponse()
        self.assertEqual(308, res.status)
        self.assertEqual("https://localhost:18443/foo", res.getheader("Location"))
        conn.close()


class TestMutualTls(unittest.TestCase):
    """Mutual TLS integration checks."""
    @classmethod
    def setUpClass(cls):
        cls.temp_dir = tempfile.mkdtemp(prefix="mtls-test-")
        cls.ca_key = os.path.join(cls.temp_dir, "ca.key")
        cls.ca_cert = os.path.join(cls.temp_dir, "ca.crt")
        cls.server_key = os.path.join(cls.temp_dir, "server.key")
        cls.server_csr = os.path.join(cls.temp_dir, "server.csr")
        cls.server_cert = os.path.join(cls.temp_dir, "server.crt")
        cls.client_key = os.path.join(cls.temp_dir, "client.key")
        cls.client_csr = os.path.join(cls.temp_dir, "client.csr")
        cls.client_cert = os.path.join(cls.temp_dir, "client.crt")

        for cmd in [
            ["openssl", "genrsa", "-out", cls.ca_key, "2048"],
            ["openssl", "req", "-x509", "-new", "-key", cls.ca_key, "-days", "3", "-subj", "/CN=TestCA", "-out", cls.ca_cert],
            ["openssl", "genrsa", "-out", cls.server_key, "2048"],
            ["openssl", "req", "-new", "-key", cls.server_key, "-subj", "/CN=localhost", "-out", cls.server_csr],
            ["openssl", "x509", "-req", "-in", cls.server_csr, "-CA", cls.ca_cert, "-CAkey", cls.ca_key, "-CAcreateserial", "-days", "3", "-out", cls.server_cert],
            ["openssl", "genrsa", "-out", cls.client_key, "2048"],
            ["openssl", "req", "-new", "-key", cls.client_key, "-subj", "/CN=client", "-out", cls.client_csr],
            ["openssl", "x509", "-req", "-in", cls.client_csr, "-CA", cls.ca_cert, "-CAkey", cls.ca_key, "-CAcreateserial", "-days", "3", "-out", cls.client_cert],
        ]:
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        os.chmod(cls.server_key, 0o600)
        os.chmod(cls.client_key, 0o600)

        cls.server = ServerHarness([
            "--http-enabled", "false",
            "--tls-enabled", "true",
            "--https-port", "19443",
            "--tls-cert", cls.server_cert,
            "--tls-key", cls.server_key,
            "--tls-ca-file", cls.ca_cert,
            "--tls-client-auth", "required",
        ])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()
        shutil.rmtree(cls.temp_dir, ignore_errors=True)

    def test_mtls_rejects_missing_client_cert(self):
        """Ensure mTLS-required listener rejects clients without certificates."""
        context = ssl.create_default_context(cafile=self.ca_cert)
        with self.assertRaises(ssl.SSLError):
            with urllib.request.urlopen("https://localhost:19443/foo", context=context, timeout=3):
                pass

    def test_mtls_accepts_valid_client_cert(self):
        """Ensure mTLS-required listener accepts trusted client certificates."""
        context = ssl.create_default_context(cafile=self.ca_cert)
        context.load_cert_chain(certfile=self.client_cert, keyfile=self.client_key)
        conn = http.client.HTTPSConnection("localhost", 19443, context=context)
        conn.request("POST", "/foo", body="secure", headers={"Content-Type": "text/plain"})
        res = conn.getresponse()
        self.assertEqual(200, res.status)
        conn.close()


class TestShortenerApi(unittest.TestCase):
    """URL shortener API + redirect behavior integration checks."""
    @classmethod
    def setUpClass(cls):
        cls.server = ServerHarness([
            "--http-port", "28080",
            "--http-enabled", "true",
            "--tls-enabled", "false",
            "--shortener-base-domain", "http://sho.rt",
            "--max-request-body-bytes", "4096",
        ])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def request(self, method, path, body=None, headers=None):
        """Issue a request to the test server and return status/body/response."""
        conn = http.client.HTTPConnection("localhost", 28080)
        conn.request(method, path, body=body, headers=headers or {})
        res = conn.getresponse()
        payload = res.read().decode("utf-8")
        conn.close()
        return res.status, payload, res

    def test_shortener_happy_path(self):
        """Cover generated slug create + root redirect + get-by-slug + get-by-id."""
        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/path"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)
        self.assertIn('"slug"', payload)
        self.assertIn('"short_url":"http://sho.rt/', payload)

        code = payload.split('"slug":"', 1)[1].split('"', 1)[0]

        status, _, res = self.request("GET", f"/{code}")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/path", res.getheader("Location"))

        status, payload, _ = self.request("GET", f"/api/v1/links/{code}")
        self.assertEqual(200, status)
        self.assertIn('"url":"https://example.com/path"', payload)

        link_id = payload.split('"id":"', 1)[1].split('"', 1)[0]
        status, payload, _ = self.request("GET", f"/api/v1/links/id/{link_id}")
        self.assertEqual(200, status)
        self.assertIn(f'"id":"{link_id}"', payload)

    def test_redirect_compat_route_and_short_url_shape(self):
        """Cover /r compatibility redirect and short_url base-domain formatting."""
        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/compat","slug":"compat01"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)
        self.assertIn('"short_url":"http://sho.rt/compat01"', payload)
        self.assertNotIn("http://sho.rt//compat01", payload)

        status, _, res = self.request("GET", "/r/compat01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/compat", res.getheader("Location"))

    def test_shortener_validation_and_conflicts(self):
        """Cover invalid URL rejection, custom slug readbacks, and slug conflicts."""
        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"ftp://invalid"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(400, status)
        self.assertIn('"invalid_url"', payload)

        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/a","slug":"my-code"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)
        self.assertIn('"slug":"my-code"', payload)

        status, payload, _ = self.request("GET", "/api/v1/links/my-code")
        self.assertEqual(200, status)
        link_id = payload.split('"id":"', 1)[1].split('"', 1)[0]

        status, payload, _ = self.request("GET", f"/api/v1/links/id/{link_id}")
        self.assertEqual(200, status)
        self.assertIn('"slug":"my-code"', payload)

        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/b","slug":"my-code"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(409, status)
        self.assertIn('"slug_conflict"', payload)

        status, _, _ = self.request("GET", "/api/v1/links/unknown-code")
        self.assertEqual(404, status)

        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/rsv","slug":"API"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(409, status)
        self.assertIn('"reserved_slug"', payload)

    def test_disabled_expired_and_permanent_redirects(self):
        """Cover 301 permanent, and 410 responses for disabled/expired links."""
        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/p","slug":"perm-301","redirect_type":"permanent"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)
        status, _, res = self.request("GET", "/perm-301")
        self.assertEqual(301, status)
        self.assertEqual("https://example.com/p", res.getheader("Location"))

        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/off","slug":"off001","enabled":false}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)
        status, payload, _ = self.request("GET", "/off001")
        self.assertEqual(410, status)
        self.assertIn('"link_disabled"', payload)

        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/exp","slug":"exp001","expires_at":"2000-01-01T00:00:00Z"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)
        status, payload, _ = self.request("GET", "/exp001")
        self.assertEqual(410, status)
        self.assertIn('"link_expired"', payload)

    def test_legacy_api_still_works(self):
        """Validate legacy URI map APIs remain functional."""
        status, _, _ = self.request("POST", "/legacy", body="data", headers={"Content-Type": "text/plain"})
        self.assertEqual(200, status)

        status, payload, _ = self.request("GET", "/legacy")
        self.assertEqual(200, status)
        self.assertIn("data", payload)

        status, _, _ = self.request("DELETE", "/legacy")
        self.assertEqual(200, status)

    def test_stage2_management_lifecycle_preview_and_stats(self):
        """Validate management lifecycle operations, preview, and stats endpoints."""
        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body=(
                '{"url":"https://example.com/mgmt","slug":"mgmt01",'
                '"tags":[" spring ","paid","paid"],'
                '"metadata":{"owner":"growth","locale":"en-US"},'
                '"campaign":{"name":"launch","source":"newsletter"}}'
            ),
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)
        self.assertIn('"tags":["spring","paid"]', payload)
        self.assertIn('"campaign":{"name":"launch","source":"newsletter"}', payload)

        status, _, _ = self.request("POST", "/api/v1/links/mgmt01/disable")
        self.assertEqual(200, status)

        status, payload, _ = self.request("GET", "/mgmt01")
        self.assertEqual(410, status)
        self.assertIn('"link_disabled"', payload)

        status, _, _ = self.request("POST", "/api/v1/links/mgmt01/enable")
        self.assertEqual(200, status)

        status, _, res = self.request("GET", "/mgmt01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/mgmt", res.getheader("Location"))

        status, payload, _ = self.request("GET", "/api/v1/links/mgmt01/stats")
        self.assertEqual(200, status)
        self.assertIn('"total_redirects":1', payload)

        status, _, _ = self.request("DELETE", "/api/v1/links/mgmt01")
        self.assertEqual(200, status)

        status, payload, _ = self.request("GET", "/mgmt01")
        self.assertEqual(404, status)
        self.assertIn('"link_deleted"', payload)

        status, payload, _ = self.request("GET", "/api/v1/links/mgmt01/preview")
        self.assertEqual(200, status)
        self.assertIn('"status":"deleted"', payload)

        status, _, _ = self.request("POST", "/api/v1/links/mgmt01/restore")
        self.assertEqual(200, status)

        status, _, res = self.request("GET", "/mgmt01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/mgmt", res.getheader("Location"))

    def test_stage3_cache_invalidation_on_lifecycle_update(self):
        """Ensure cache invalidation reflects enable/disable lifecycle updates."""
        status, _, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/cache","slug":"cache01"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)

        status, _, res = self.request("GET", "/cache01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/cache", res.getheader("Location"))

        status, _, _ = self.request("POST", "/api/v1/links/cache01/disable")
        self.assertEqual(200, status)

        status, payload, _ = self.request("GET", "/cache01")
        self.assertEqual(410, status)
        self.assertIn('"link_disabled"', payload)

        status, _, _ = self.request("POST", "/api/v1/links/cache01/enable")
        self.assertEqual(200, status)

        status, _, res = self.request("GET", "/cache01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/cache", res.getheader("Location"))

    def test_stage2_patch_and_expiry(self):
        """Ensure PATCH expiry updates affect redirect and preview status."""
        status, _, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/patch","slug":"patch01"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)

        status, payload, _ = self.request(
            "PATCH",
            "/api/v1/links/patch01",
            body='{"expires_at":"2000-01-01T00:00:00Z"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(200, status)
        self.assertIn('"expires_at":"2000-01-01T00:00:00Z"', payload)

        status, payload, _ = self.request("GET", "/patch01")
        self.assertEqual(410, status)
        self.assertIn('"link_expired"', payload)

        status, payload, _ = self.request("GET", "/api/v1/links/patch01/preview")
        self.assertEqual(200, status)
        self.assertIn('"status":"expired"', payload)

    def test_stage5_request_id_propagation(self):
        """Ensure request-id propagation and sanitization work as expected."""
        request_id = "req-abc.123_TEST"
        status, payload, res = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/stage5","slug":"obs001"}',
            headers={"Content-Type": "application/json", "X-Request-Id": request_id},
        )
        self.assertEqual(201, status)
        self.assertEqual(request_id, res.getheader("X-Request-Id"))

        status, payload, res = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"ftp://bad"}',
            headers={"Content-Type": "application/json", "X-Request-Id": "bad id with spaces"},
        )
        self.assertEqual(400, status)
        response_request_id = res.getheader("X-Request-Id")
        self.assertTrue(response_request_id)
        self.assertNotEqual("bad id with spaces", response_request_id)
        self.assertIn(f'"request_id":"{response_request_id}"', payload)

    def test_stage5_metrics_and_payload_limit(self):
        """Ensure payload limits and metrics endpoint behavior are enforced."""
        status, _, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/metric","slug":"met001"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)

        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/' + ('x' * 9000) + '"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(413, status)
        self.assertIn('"payload_too_large"', payload)

        status, payload, _ = self.request("GET", "/metrics")
        self.assertEqual(200, status)
        self.assertIn("http_requests_total", payload)
        self.assertIn("http_malformed_requests_total", payload)


class TestStage4AnalyticsQueue(unittest.TestCase):
    """Ensure analytics queue saturation does not break redirects."""
    @classmethod
    def setUpClass(cls):
        cls.server = ServerHarness([
            "--http-port", "38080",
            "--http-enabled", "true",
            "--tls-enabled", "false",
            "--analytics-enabled", "true",
            "--analytics-queue-capacity", "0",
            "--analytics-client-hash-salt", "test-salt",
        ])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def request(self, method, path, body=None, headers=None):
        """Issue a request to analytics test server and return status/body/response."""
        conn = http.client.HTTPConnection("localhost", 38080)
        conn.request(method, path, body=body, headers=headers or {})
        res = conn.getresponse()
        payload = res.read().decode("utf-8")
        conn.close()
        return res.status, payload, res

    def test_redirects_work_with_saturated_analytics_queue(self):
        """Ensure redirect behavior remains correct when analytics queue overflows."""
        status, payload, _ = self.request(
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/analytics","slug":"anl001"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(201, status)

        for _ in range(20):
            status, _, res = self.request(
                "GET",
                "/anl001",
                headers={
                    "User-Agent": "x" * 2048,
                    "Referer": "https://example.com/ref/" + ("y" * 2048),
                    "X-Forwarded-For": "203.0.113.9",
                },
            )
            self.assertEqual(302, status)
            self.assertEqual("https://example.com/analytics", res.getheader("Location"))

        status, payload, _ = self.request("GET", "/api/v1/links/anl001/stats")
        self.assertEqual(200, status)
        self.assertIn('"total_redirects":20', payload)


if __name__ == "__main__":
    unittest.main()
