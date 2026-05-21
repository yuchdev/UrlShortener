"""Shared helpers for link-management integration executable tests."""

import os
import sys
import unittest

CURRENT_DIR = os.path.dirname(__file__)
CORE_DIR = os.path.abspath(os.path.join(CURRENT_DIR, "..", "core"))
if CORE_DIR not in sys.path:
    sys.path.insert(0, CORE_DIR)

from integration_common import ServerHarness, request  # noqa: E402


def create_link(port, slug, url="https://example.com/default", extra_payload=""):
    payload = '{"url":"' + url + '","slug":"' + slug + '"'
    if extra_payload:
        payload += ',' + extra_payload
    payload += '}'
    return request(
        port,
        "POST",
        "/api/v1/links",
        body=payload,
        headers={"Content-Type": "application/json"},
    )


class LinkManagementIntegrationBase(unittest.TestCase):
    port = None
    server = None

    @classmethod
    def setUpClass(cls):
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
