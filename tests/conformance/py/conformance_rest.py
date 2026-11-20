#!/usr/bin/env python3
"""conformance_rest.py - shared REST server/client for the corpus REST shims.

The two REST corpus shims - ``13_rest_generic_corpus.py`` (``POST /api/v1/commands``,
subtask 08) and the resource-route shim (subtask 09) - both need to start a real
``url_shortener`` HTTP server, wait for it to answer, route requests to it and
reap it afterwards. That machinery lives here once so neither shim duplicates it.

Security model (threat brief ruling (c)):

  * One server **per role**. The admin server is started with the real
    ``--command-api-role admin`` operator flag; because every connection targets
    ``127.0.0.1`` the peer is loopback, so ``command_api_caller``
    (``src/http/command_bridge.cpp``) grants Admin. The user server is started
    with the default ``--command-api-role user`` and stays User. The shim never
    grants blanket Admin to every case and never passes a role through the
    request body.
  * File paths are left at the production default-deny: no ``--path-root`` or
    ``--no-file-paths`` flag is ever passed, so a ``path`` argument comes back
    ``unsupported``.
  * The *client* connects only to ``127.0.0.1``. The *server*, however, listens
    on all IPv4 interfaces: ``HttpServer`` always binds ``0.0.0.0``
    (``src/http/http_server.cpp``) and exposes no bind-host flag - that is by
    design for the public redirect server, and Admin is still gated on a loopback
    *peer* (``is_loopback_peer`` / ``command_api_caller`` in
    ``src/http/command_bridge.cpp``), not on the bind address. The test-window
    exposure this implies: while a case runs, any **local** process able to reach
    the ephemeral port over loopback would be granted Admin on that throwaway
    server's per-case SQLite database. This is acceptable for a short-lived test
    server on a developer/CI host and is not changed here (no product code is
    touched); it is called out so the loopback-client wording is not read as a
    loopback *bind*.
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
from typing import Dict, List, Optional, Tuple

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402

#: Maximum seconds to wait for a freshly spawned server to answer ``/healthz``.
_SERVER_READY_TIMEOUT_S = 15.0

#: Hard per-request wall clock; a hung server fails the step by timeout instead
#: of blocking CTest.
_POST_TIMEOUT_S = 10.0

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
    """POSTs a command envelope to the server matching a role.

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
