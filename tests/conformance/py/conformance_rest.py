#!/usr/bin/env python3
"""conformance_rest.py - shared REST server/client for the corpus REST shims.

The two REST corpus shims - ``13_rest_generic_corpus.py`` (``POST /api/v1/commands``,
subtask 08) and the resource-route shim (subtask 09) - both need to start a real
``url_shortener`` HTTP server, wait for it to answer, route requests to it and
reap it afterwards. That machinery lives here once so neither shim duplicates it.

Security model (threat brief ruling (c)):

  * One server **per role**. The admin server is started with the real
    ``--command-api-role admin`` operator flag; for the gated endpoints (generic
    ``POST /api/v1/commands`` and the gated reads ``GET /api/v1/links`` /
    ``/api/v1/jobs*``), because every connection targets ``127.0.0.1`` the peer is
    loopback, so ``command_api_caller`` (``src/http/command_bridge.cpp``) grants
    Admin. The user server is started with the default ``--command-api-role user``
    and stays User on those endpoints. The shim never grants blanket Admin to
    every case and never passes a role through the request body.
  * File paths are left at the production default-deny: no ``--path-root`` or
    ``--no-file-paths`` flag is ever passed, so a ``path`` argument comes back
    ``unsupported``.
  * The *client* connects only to ``127.0.0.1``. The *server*, however, listens
    on all IPv4 interfaces: ``HttpServer`` always binds ``0.0.0.0``
    (``src/http/http_server.cpp``) and exposes no bind-host flag - that is by
    design for the public redirect server. The gated endpoints above derive the
    role from a loopback *peer* (``is_loopback_peer`` / ``command_api_caller``),
    not from the bind address. The **resource write routes**
    (``link.create``/``update``/``delete``/``enable``/``disable``/``restore``),
    however, run through ``resource_route_caller()``, which is hard-wired to
    ``ControlSet::Admin`` with **no** peer or role check
    (``src/http/command_bridge.cpp``) - the documented T-60 open-routes posture
    deferred to Milestone 0002. The test-window exposure this implies: while a
    case runs, **any network peer** able to reach the ephemeral ``0.0.0.0`` port
    (not only a loopback one) can perform Admin-level resource writes against that
    throwaway server's per-case SQLite database, and a loopback peer additionally
    gains Admin on the gated endpoints. This is acceptable for a short-lived test
    server on a developer/CI host and is not changed here (no product code is
    touched); it is called out so the loopback-client wording is not read as a
    loopback *bind* and so the resource-route exposure is not understated.
  * Every HTTP call is timeout-bounded, so a hung server fails a step by timeout
    rather than blocking CTest.

The module is Python standard library only and runs under ``python3 -I``; the
sibling ``conformance_common`` import is resolved from ``__file__``.
"""

from __future__ import annotations

import http.client
import json
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import urlencode

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402

#: Maximum seconds to wait for a freshly spawned server to answer ``/healthz``.
#: Scaled by ``URLSHORTENER_TEST_TIMEOUT_SCALE`` so a slow sanitizer build of the
#: server is still given time to become ready.
_SERVER_READY_TIMEOUT_S = cc.scale_timeout(15.0)

#: Hard per-request wall clock; a hung server fails the step by timeout instead
#: of blocking CTest. Scaled by ``URLSHORTENER_TEST_TIMEOUT_SCALE`` for slow
#: sanitizer builds.
_POST_TIMEOUT_S = cc.scale_timeout(10.0)

#: Start attempts: :func:`conformance_common.free_port` races with re-binding, so
#: a server that exits early (port grabbed in between) is retried on a new port.
_START_ATTEMPTS = 3

#: Bytes of the server log tailed into a start-failure message.
_LOG_TAIL_BYTES = 2000

#: Hard cap on a single response body read. A conformance envelope is tiny (well
#: under a KiB); this ceiling bounds memory if a server ever streams an unbounded
#: body, failing the step clearly instead of reading without limit.
_MAX_RESPONSE_BYTES = 4 * 1024 * 1024


class ServerProcess:
    """One ``url_shortener`` HTTP server bound to a free loopback port.

    The server runs with the generic command API enabled under a fixed
    command-api role. Its stdout and stderr are redirected to a log file in the
    per-case temp dir, whose tail is attached to a start failure. The process is
    always reaped through :func:`conformance_common.terminate_and_reap`.

    :ivar port: The loopback port the server is listening on once :meth:`start`
        returns, else ``0``.
    """

    def __init__(
        self,
        binary: str,
        storage_args: List[str],
        role: str,
        log_path: Path,
        cwd: Optional[str] = None,
        env: Optional[Dict[str, str]] = None,
    ) -> None:
        """Record how to start the server; nothing is spawned until :meth:`start`.

        :param binary: Absolute path to the ``url_shortener`` executable.
        :param storage_args: The backend selector argv (``["--storage-config",
            <path>]`` for a sqlite case) appended verbatim.
        :param role: ``"admin"`` or ``"user"`` - the ``--command-api-role`` value;
            Admin is honoured only because the shim connects over loopback.
        :param log_path: File the server's stdout/stderr are appended to.
        :param cwd: Working directory for the child (``None`` inherits).
        :param env: Full environment for the child (``None`` inherits).
        """
        self._binary = binary
        self._storage_args = list(storage_args)
        self._role = role
        self._log_path = Path(log_path)
        self._cwd = cwd
        self._env = env
        self.port = 0
        self._proc: Optional["cc.subprocess.Popen"] = None

    def start(self) -> None:
        """Spawn the server and block until ``/healthz`` answers 200.

        Retries up to :data:`_START_ATTEMPTS` times, each on a fresh free port,
        when the child exits early (a lost free-port race) before it is ready.

        :raises RuntimeError: If no attempt became ready; the message carries the
            last exit code and the tail of the server log.
        """
        failures: List[str] = []
        for attempt in range(_START_ATTEMPTS):
            port = cc.free_port()
            with open(self._log_path, "ab") as log:
                argv = [
                    self._binary,
                    "--http-port", str(port),
                    "--command-api-enabled", "true",
                    "--command-api-role", self._role,
                    *self._storage_args,
                ]
                proc = cc.spawn(argv, cwd=self._cwd, env=self._env,
                                stdout=log, stderr=log)
            if self._await_ready(proc, port):
                self.port = port
                self._proc = proc
                return
            cc.terminate_and_reap(proc)
            failures.append(f"attempt {attempt + 1} on port {port}: "
                            f"rc={proc.returncode}")
        raise RuntimeError(
            f"url_shortener (role {self._role}) not ready after "
            f"{_START_ATTEMPTS} attempts ({'; '.join(failures)}); "
            f"server log tail:\n{self._log_tail()}"
        )

    def _await_ready(self, proc: "cc.subprocess.Popen", port: int) -> bool:
        """Poll readiness while watching for an early child exit.

        :param proc: The spawned server process.
        :param port: The loopback port it was told to bind.
        :return: ``True`` once ``/healthz`` answers 200; ``False`` if the child
            exits first or the readiness deadline passes.
        """
        deadline = time.monotonic() + _SERVER_READY_TIMEOUT_S
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                return False  # early exit (likely a port clash) -> caller retries
            try:
                cc.wait_http_ready(port, timeout=0.5)
                return True
            except TimeoutError:
                continue
        return False

    def _log_tail(self) -> str:
        """Return the last :data:`_LOG_TAIL_BYTES` of the server log (never raises)."""
        try:
            text = self._log_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return "<server log unavailable>"
        return text[-_LOG_TAIL_BYTES:]

    def stop(self) -> None:
        """Terminate and reap the server; idempotent and safe on every path."""
        cc.terminate_and_reap(self._proc)
        self._proc = None


class RestClient:
    """Issues HTTP requests to the server matching a role, over loopback.

    Two request shapes are supported, both bounded by :data:`_POST_TIMEOUT_S`,
    both targeting only ``127.0.0.1`` and both capping the response body at
    :data:`_MAX_RESPONSE_BYTES`:

      * :meth:`post_command` - the generic command endpoint
        ``POST /api/v1/commands`` used by the ``rest_generic`` shim (subtask 08).
      * :meth:`call` - an arbitrary method/path/query/body resource-route request
        used by the ``rest_resource`` shim (subtask 09). It never touches the
        generic path above.

    :ivar _ports: A live ``role -> port`` mapping; the owner mutates it as it
        lazily spawns the per-role servers, so a client built up front still
        resolves the right port once a role's server is up.
    """

    def __init__(self, ports: Dict[str, int]) -> None:
        """Bind to a live ``role -> port`` map.

        :param ports: The mapping the owning adapter updates as servers start;
            kept by reference so lazily-spawned servers are visible here.
        """
        self._ports = ports

    def post_command(
        self, role: str, body: str
    ) -> Tuple[int, Optional[dict], str, str]:
        """POST *body* to ``/api/v1/commands`` on the *role* server over loopback.

        The request always sets ``Content-Type: application/json`` and carries no
        ``Authorization`` or other secret header, so failure output never leaks a
        token. The call is bounded by :data:`_POST_TIMEOUT_S`.

        :param role: The role whose server receives the request; must be present
            in the live port map.
        :param body: The verbatim request text (a JSON envelope, or a
            ``raw_request`` string).
        :return: ``(http_status, parsed_json_or_None, raw_text, content_type)``;
            *parsed_json_or_None* is ``None`` when the body is not valid JSON.
        :raises KeyError: If no server for *role* has been started.
        :raises AssertionError: If the response body exceeds
            :data:`_MAX_RESPONSE_BYTES`.
        """
        port = self._ports[role]
        conn = http.client.HTTPConnection("127.0.0.1", port,
                                          timeout=_POST_TIMEOUT_S)
        try:
            conn.request("POST", "/api/v1/commands",
                         body=body.encode("utf-8"),
                         headers={"Content-Type": "application/json"})
            response = conn.getresponse()
            # Read one byte past the cap so an over-cap body is detected exactly.
            body_bytes = response.read(_MAX_RESPONSE_BYTES + 1)
            status = response.status
            content_type = response.getheader("Content-Type", "") or ""
        finally:
            conn.close()
        if len(body_bytes) > _MAX_RESPONSE_BYTES:
            raise AssertionError(
                f"rest_generic response body exceeded {_MAX_RESPONSE_BYTES} "
                f"bytes (status {status}); refusing to buffer it")
        raw = body_bytes.decode("utf-8", "replace")
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            parsed = None
        return status, parsed, raw, content_type

    def call(
        self,
        role: str,
        method: str,
        path: str,
        query: Optional[Any] = None,
        body: Optional[Any] = None,
    ) -> Tuple[int, Optional[dict], str]:
        """Issue a resource-route request on the *role* server over loopback.

        This is the resource-route counterpart to :meth:`post_command`: it sends
        a free method/path/query/body instead of a command envelope to
        ``/api/v1/commands``, so the ``rest_resource`` shim can replay the
        equivalent REST resource routes (``POST /api/v1/links``,
        ``GET /api/v1/links/{slug}``, ...). It carries no ``Authorization`` or
        other secret header, so failure output never leaks a token, and it is
        bounded by :data:`_POST_TIMEOUT_S`.

        :param role: The role whose server receives the request; must be present
            in the live port map.
        :param method: The HTTP method (``"GET"``, ``"POST"``, ``"PATCH"``,
            ``"DELETE"``), used verbatim.
        :param path: The request path, e.g. ``"/api/v1/links/docs"``; it is sent
            exactly as given (no percent-encoding is applied, so a corpus author
            controls on-the-wire bytes).
        :param query: Optional query parameters: a ``dict`` (percent-encoded via
            :func:`urllib.parse.urlencode`) or a raw string (a leading ``?`` is
            tolerated and stripped). ``None`` or empty sends no query.
        :param body: Optional request body: when not ``None`` it is JSON-encoded
            and sent with ``Content-Type: application/json``; otherwise no body
            and no content-type header are sent.
        :return: ``(http_status, parsed_json_or_None, raw_text)``;
            *parsed_json_or_None* is ``None`` when the body is not valid JSON.
        :raises KeyError: If no server for *role* has been started.
        :raises AssertionError: If the response body exceeds
            :data:`_MAX_RESPONSE_BYTES`.
        """
        port = self._ports[role]
        target = path
        query_text = ""
        if isinstance(query, dict):
            query_text = urlencode(query)
        elif isinstance(query, str):
            query_text = query[1:] if query.startswith("?") else query
        if query_text:
            target = f"{path}?{query_text}"
        headers: Dict[str, str] = {}
        encoded_body: Optional[bytes] = None
        if body is not None:
            encoded_body = json.dumps(body, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json"
        conn = http.client.HTTPConnection("127.0.0.1", port,
                                          timeout=_POST_TIMEOUT_S)
        try:
            conn.request(method, target, body=encoded_body, headers=headers)
            response = conn.getresponse()
            # Read one byte past the cap so an over-cap body is detected exactly.
            body_bytes = response.read(_MAX_RESPONSE_BYTES + 1)
            status = response.status
        finally:
            conn.close()
        if len(body_bytes) > _MAX_RESPONSE_BYTES:
            raise AssertionError(
                f"rest_resource response body exceeded {_MAX_RESPONSE_BYTES} "
                f"bytes (status {status}); refusing to buffer it")
        raw = body_bytes.decode("utf-8", "replace")
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            parsed = None
        return status, parsed, raw

    def call_with_allow(
        self,
        role: str,
        method: str,
        path: str,
        query: Optional[Any] = None,
        body: Optional[Any] = None,
    ) -> Tuple[int, Optional[dict], str, str]:
        """Like :meth:`call`, but also return the response ``Allow`` header.

        Used by method-mismatch rows that assert the RFC 9110 15.5.6 contract:
        a 405 Method Not Allowed names the supported methods in ``Allow``. It
        issues the same loopback, timeout- and size-bounded request as
        :meth:`call` and sends no ``Authorization`` or other secret header.

        :param role: The role whose server receives the request; must be present
            in the live port map.
        :param method: The HTTP method, used verbatim.
        :param path: The request path, sent exactly as given.
        :param query: Optional query parameters (``dict`` or raw string); see
            :meth:`call`.
        :param body: Optional JSON request body; see :meth:`call`.
        :return: ``(http_status, parsed_json_or_None, raw_text, allow_header)``;
            *allow_header* is ``""`` when the response carries no ``Allow``.
        :raises KeyError: If no server for *role* has been started.
        :raises AssertionError: If the response body exceeds
            :data:`_MAX_RESPONSE_BYTES`.
        """
        port = self._ports[role]
        target = path
        query_text = ""
        if isinstance(query, dict):
            query_text = urlencode(query)
        elif isinstance(query, str):
            query_text = query[1:] if query.startswith("?") else query
        if query_text:
            target = f"{path}?{query_text}"
        headers: Dict[str, str] = {}
        encoded_body: Optional[bytes] = None
        if body is not None:
            encoded_body = json.dumps(body, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json"
        conn = http.client.HTTPConnection("127.0.0.1", port,
                                          timeout=_POST_TIMEOUT_S)
        try:
            conn.request(method, target, body=encoded_body, headers=headers)
            response = conn.getresponse()
            body_bytes = response.read(_MAX_RESPONSE_BYTES + 1)
            status = response.status
            allow = response.getheader("Allow", "") or ""
        finally:
            conn.close()
        if len(body_bytes) > _MAX_RESPONSE_BYTES:
            raise AssertionError(
                f"rest_resource response body exceeded {_MAX_RESPONSE_BYTES} "
                f"bytes (status {status}); refusing to buffer it")
        raw = body_bytes.decode("utf-8", "replace")
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            parsed = None
        return status, parsed, raw, allow
