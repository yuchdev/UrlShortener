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
    def __init__(self, args):
        self.args = args
        self.process = None

    def start(self):
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
        if self.process and self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)


class TestHttpAndHttps(unittest.TestCase):
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
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.minimum_version = ssl.TLSVersion.TLSv1_1
        context.maximum_version = ssl.TLSVersion.TLSv1_1
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        with self.assertRaises((ssl.SSLError, urllib.error.URLError)):
            with urllib.request.urlopen("https://localhost:18443", context=context, timeout=3):
                pass

    def test_http_redirects_to_https(self):
        conn = http.client.HTTPConnection("localhost", 18080)
        conn.request("GET", "/foo", headers={"Host": "localhost:18080"})
        res = conn.getresponse()
        self.assertEqual(308, res.status)
        self.assertEqual("https://localhost:18443/foo", res.getheader("Location"))
        conn.close()


class TestMutualTls(unittest.TestCase):
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
        context = ssl.create_default_context(cafile=self.ca_cert)
        with self.assertRaises(ssl.SSLError):
            with urllib.request.urlopen("https://localhost:19443/foo", context=context, timeout=3):
                pass

    def test_mtls_accepts_valid_client_cert(self):
        context = ssl.create_default_context(cafile=self.ca_cert)
        context.load_cert_chain(certfile=self.client_cert, keyfile=self.client_key)
        conn = http.client.HTTPSConnection("localhost", 19443, context=context)
        conn.request("POST", "/foo", body="secure", headers={"Content-Type": "text/plain"})
        res = conn.getresponse()
        self.assertEqual(200, res.status)
        conn.close()


class TestShortenerApi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ServerHarness([
            "--http-port", "28080",
            "--http-enabled", "true",
            "--tls-enabled", "false",
            "--shortener-base-domain", "http://sho.rt",
        ])
        cls.server.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection("localhost", 28080)
        conn.request(method, path, body=body, headers=headers or {})
        res = conn.getresponse()
        payload = res.read().decode("utf-8")
        conn.close()
        return res.status, payload, res

    def test_shortener_happy_path(self):
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

    def test_shortener_validation_and_conflicts(self):
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

    def test_disabled_expired_and_permanent_redirects(self):
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
        status, _, _ = self.request("POST", "/legacy", body="data", headers={"Content-Type": "text/plain"})
        self.assertEqual(200, status)

        status, payload, _ = self.request("GET", "/legacy")
        self.assertEqual(200, status)
        self.assertIn("data", payload)

        status, _, _ = self.request("DELETE", "/legacy")
        self.assertEqual(200, status)


if __name__ == "__main__":
    unittest.main()
