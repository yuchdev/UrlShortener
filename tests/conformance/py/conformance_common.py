#!/usr/bin/env python3
"""conformance_common.py - shared harness for the out-of-process command shims.

The command conformance corpus (``tests/conformance/commands/*.json``) is replayed
through six transports. Four of them run out of process and are driven from
Python: the ``cmd`` CLI (subtask 06), ``serve-stdio`` (07), the REST generic
endpoint (08) and the REST resource routes (09). This module is the one place all
four share: corpus loading, variable handling, the :class:`Adapter` base class,
per-case temp storage, binary discovery, free-port allocation and the
:func:`run_case` executor that mirrors the C++ ``CaseRunner`` of subtask 03.

Each shim then only has to implement "send one request envelope and return the
parsed response envelope".

Matching and normalization are **not** reimplemented here: this module delegates
to :mod:`conformance_match` (subtask 02), whose rules are pinned as data in
``tests/conformance/normalizer_vectors.json`` so the Python and C++ matchers
cannot drift. The ``HTTP_STATUS`` / ``CLI_EXIT`` tables below are exact copies of
the command catalog's error-code table (subtask 16 asserts the equality);
:func:`run_case` asserts the transport status an adapter reports against them,
and treats a missing status on a transport that has a status channel as a
finding so a shim cannot pass without it.

How to add an adapter (5 steps)
-------------------------------
1. Subclass :class:`Adapter`, set ``self.transport`` to one of the ids in
   :data:`TRANSPORT_CAPS` (its capabilities drive the id/raw/options skip rules).
2. In ``begin_case`` (optional) start whatever the case needs - spawn the binary
   with :func:`spawn` (never a shell), materialize ``ctx.storage_config_path()``
   for a sqlite case, wait for readiness with :func:`wait_http_ready` - or return
   a reason string to skip.
3. Implement ``send(step, request_text, ctx)`` with a bounded timeout: turn the
   already-substituted request into a wire call and return the response. For
   ``rest_*`` return ``{"envelope": {...}, "http_status": int}`` and for
   ``cli_cmd`` ``{"envelope": {...}, "cli_exit": int}`` so :func:`run_case`
   asserts the transport status; ``direct`` / ``stdio`` / ``tui`` return the
   plain envelope ``dict``.
4. In ``end_case`` (optional) stop and reap every process you started with
   :func:`terminate_and_reap` (terminate -> wait -> kill -> wait); never
   ``shell=True``.
5. Register a ``NN_<name>_shim.py`` that calls ``main_for(MyAdapter, "<id>")``
   and append it to ``CONFORMANCE_PY_SOURCES`` in ``sources.cmake``.

The module is Python standard library only and runs under ``python3 -I``: every
path is resolved from ``__file__`` and the sibling directory is inserted onto
``sys.path`` explicitly.
"""

from __future__ import annotations

import http.client
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from abc import ABC, abstractmethod
from collections import namedtuple
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple

# Resolve every path from this file, never the process cwd, and make the sibling
# ``conformance_match`` module importable under ``python3 -I`` (isolated mode).
_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_match as cm  # noqa: E402

#: ``tests/conformance`` - the parent of this ``py`` directory.
_CONFORMANCE_DIR = _THIS.parents[1]

#: Repository root (``tests/conformance/py`` -> three levels up).
_REPO_ROOT = _THIS.parents[3]


# ---------------------------------------------------------------------------
# Sanitizer-aware timeout scaling
# ---------------------------------------------------------------------------


def timeout_scale() -> float:
    """Return the multiplier applied to this harness's hard-coded timeouts.

    A ThreadSanitizer/AddressSanitizer build of ``url_shortener`` runs the
    spawned child process several times slower, so its own fixed
    ``subprocess``/socket/readiness timeouts (e.g. ``_SEND_TIMEOUT_S``) fire
    before CTest's per-test ``TIMEOUT`` and surface as spurious failures. CMake
    detects the sanitizer flags, multiplies the CTest ``TIMEOUT`` property by a
    factor, and exports the same factor as ``URLSHORTENER_TEST_TIMEOUT_SCALE``
    so each harness widens its internal deadlines in lockstep. ``python3 -I``
    ignores only ``PYTHON*`` environment variables, so this non-prefixed one is
    still visible.

    @return The scale as a float. A missing, empty, non-numeric or
            non-positive ``URLSHORTENER_TEST_TIMEOUT_SCALE`` yields ``1.0`` so
            that normal (non-sanitizer) builds are left completely unchanged.
    """
    raw = os.environ.get("URLSHORTENER_TEST_TIMEOUT_SCALE", "")
    try:
        value = float(raw)
    except (TypeError, ValueError):
        return 1.0
    return value if value > 0.0 else 1.0


def scale_timeout(base_seconds: float) -> float:
    """Scale a base timeout (in seconds) by :func:`timeout_scale`.

    @param base_seconds The harness's nominal timeout for a normal build.
    @return ``base_seconds * timeout_scale()``; equals ``base_seconds`` exactly
            when no scaling is configured.
    """
    return base_seconds * timeout_scale()


# ---------------------------------------------------------------------------
# Transport capabilities and the catalog status/exit tables
# ---------------------------------------------------------------------------

#: One transport's three replay capabilities. ``id`` - the transport echoes the
#: request envelope ``id`` (otherwise the server mints ``srv-<n>``, see N4);
#: ``raw`` - the transport can carry a verbatim ``raw_request`` string; and
#: ``options`` - the transport can convey the envelope ``options`` object.
Caps = namedtuple("Caps", ["id", "raw", "options"])

#: Per-transport capabilities; kept in sync with ``TransportCaps`` in the C++
#: support library (subtask 03). The ``cli_cmd`` options flag is UNVERIFIED -
#: it depends on Task 04.0 exposing ``dry_run`` / ``async`` as CLI flags.
TRANSPORT_CAPS: Dict[str, Caps] = {
    "direct": Caps(id=True, raw=True, options=True),
    "rest_generic": Caps(id=True, raw=True, options=True),
    "rest_resource": Caps(id=False, raw=False, options=False),
    "cli_cmd": Caps(id=True, raw=False, options=True),
    "stdio": Caps(id=True, raw=True, options=True),
    "tui": Caps(id=True, raw=False, options=True),
}

#: Regex (anchored by :func:`re.fullmatch`) that a *server-minted* envelope id -
#: the id a transport returns when the request carried none - must match, per
#: transport. N4 defaults every transport to ``srv-<n>``; the ``cmd`` CLI instead
#: mints its own client id ``cli-<n>`` (``src/cli/cmd_dispatch.cpp``), which is a
#: Python-only transport rule (``cli_cmd`` has no C++ runner), so it does not
#: belong in the shared N4 vectors. A transport absent here uses the default.
_GENERATED_ID_PATTERN: Dict[str, str] = {
    "cli_cmd": "cli-[0-9]+",
}

#: Default server-minted id shape (N4), used for every transport not overridden
#: in :data:`_GENERATED_ID_PATTERN`.
_DEFAULT_GENERATED_ID_PATTERN = "srv-[0-9]+"

#: Out-of-process transports that cannot share an in-process ``inmemory`` store
#: (corpus lint rule 10). Only ``direct`` and ``tui`` run in the test process.
OUT_OF_PROCESS_TRANSPORTS = {"cli_cmd", "stdio", "rest_generic", "rest_resource"}

#: Which transport-status channel each transport exposes, so :func:`run_case`
#: can require and assert it. ``"http"`` -> the adapter must return an HTTP
#: status; ``"cli"`` -> a process exit code; ``None`` -> no separate status
#: channel (``direct`` is in-process, ``stdio`` / ``tui`` frame one envelope).
_STATUS_CHANNEL: Dict[str, Optional[str]] = {
    "direct": None,
    "rest_generic": "http",
    "rest_resource": "http",
    "cli_cmd": "cli",
    "stdio": None,
    "tui": None,
}

#: Error code -> HTTP status for the REST transports. An exact copy of the
#: command catalog error table (01.0/02); subtask 16 asserts the equality.
HTTP_STATUS: Dict[str, int] = {
    "invalid_envelope": 400,
    "unknown_command": 404,
    "invalid_field": 400,
    "invalid_url": 400,
    "invalid_slug": 400,
    "reserved_slug": 409,
    "slug_conflict": 409,
    "not_found": 404,
    "not_authorized": 403,
    "unsupported": 501,
    "busy": 503,
    "job_cancelled": 200,
    "storage_failure": 500,
    "internal": 500,
}

#: Error code -> ``cmd`` process exit code. An exact copy of the 01.0/02
#: error-code table; subtask 16 asserts the equality.
CLI_EXIT: Dict[str, int] = {
    "invalid_envelope": 2,
    "unknown_command": 2,
    "invalid_field": 2,
    "invalid_url": 2,
    "invalid_slug": 2,
    "reserved_slug": 2,
    "slug_conflict": 3,
    "not_found": 1,
    "not_authorized": 5,
    "unsupported": 2,
    "busy": 4,
    "job_cancelled": 4,
    "storage_failure": 4,
    "internal": 4,
}

#: Job lifecycle states that terminate polling (``is_terminal`` in
#: ``src/commands/job_store.cpp``).
_TERMINAL_JOB_STATES = {"succeeded", "failed", "cancelled"}

#: Poll interval while awaiting a job's terminal state (mirrors the C++ runner).
_JOB_POLL_INTERVAL_S = 0.025

#: Hard ceiling on ``await_job.timeout_ms`` so a corpus typo cannot hang CTest;
#: a larger value is clamped down to this.
_MAX_AWAIT_TIMEOUT_MS = 60000


# ---------------------------------------------------------------------------
# Per-case context
# ---------------------------------------------------------------------------


def _bad_file_name_reason(key: str) -> Optional[str]:
    """Return why *key* is an unsafe ``files`` name, or ``None`` if it is safe.

    Mirrors corpus lint rule 11: a ``files`` key is materialized as a bare file
    name inside the per-case temp dir, so it must not be empty, contain a path
    separator, be a directory alias (``.`` / ``..``) or start with ``~``.

    :param key: The ``files`` map key to validate.
    :return: A short human reason, or ``None`` when the key is safe.
    """
    if key == "":
        return "empty name"
    if "/" in key or "\\" in key:
        return "contains a path separator"
    if key in (".", ".."):
        return "is a directory alias"
    if key.startswith("~"):
        return "starts with '~'"
    return None


class CaseContext:
    """Mutable per-case state shared between :func:`run_case` and an adapter.

    One instance is created for each case. It owns a unique temp directory into
    which ``files`` are materialized and a sqlite ``--storage-config`` document
    can be written, plus the variable map that ``save`` / ``${var}`` use. The
    directory is removed by :meth:`cleanup` on every path.

    :ivar tmp: The per-case temp directory (unique, owned unless injected).
    :ivar backend: Effective backend id (``sqlite`` default, or ``inmemory``).
    :ivar variables: Variable name -> saved value map (``${tmp}`` is added by the
        runner at substitution time; it is not stored here).
    :ivar storage_args: The exact argv a shim appends to select this case's
        backend: ``["--storage-config", <path>]`` for sqlite, else ``[]``. Kept
        in one place so shims never build the flag themselves.
    """

    def __init__(self, case: Dict[str, Any], tmp_dir: Optional[str] = None) -> None:
        """Create the context and its temp directory (files are not yet written).

        :param case: The case dict (only ``backend`` is read here).
        :param tmp_dir: Testing hook; when given, use this path as the per-case
            temp dir verbatim and do **not** delete it on cleanup (used by the
            self-test to inject a Windows-style ``${tmp}`` value). Production
            callers pass ``None`` so a fresh, owned directory is created.
        """
        self.backend: str = case.get("backend", "sqlite")
        self.variables: Dict[str, Any] = {}
        if tmp_dir is None:
            self._owns_tmp = True
            self.tmp = Path(tempfile.mkdtemp(prefix="conf_case_"))
        else:
            self._owns_tmp = False
            self.tmp = Path(tmp_dir)
        self._storage_yaml = self.tmp / "storage.yaml"
        if self.backend == "sqlite":
            self.storage_args: List[str] = [
                "--storage-config",
                str(self._storage_yaml),
            ]
        else:
            self.storage_args = []

    def materialize_files(self, case: Dict[str, Any]) -> None:
        """Write every ``files`` entry into :attr:`tmp` as a bare safe name.

        :param case: The case dict; its optional ``files`` object maps a bare
            file name to UTF-8 text content.
        :raises ValueError: If a key is not a safe bare name (lint rule 11); the
            runner turns this into a clean finding rather than a crash.
        """
        # The temp dir comes from tempfile.mkdtemp, which creates it mode 0700
        # (owner-only), so case files are not world-readable while a case runs.
        files = case.get("files") or {}
        for name, content in files.items():
            reason = _bad_file_name_reason(name)
            if reason is not None:
                raise ValueError(f"unsafe files key {name!r}: {reason}")
            (self.tmp / name).write_text(str(content), encoding="utf-8")

    def storage_config_path(self) -> Path:
        """Write a SQLite ``--storage-config`` YAML into :attr:`tmp` and return it.

        The document is the minimal shape ``ParseStorageConfigYaml`` accepts (see
        ``docs/storage/configuration.md``): a ``sqlite`` metadata backend whose
        database file lives in the same temp directory. The path uses forward
        slashes so the YAML is valid on Windows too.

        :return: Absolute path to the written ``storage.yaml``.
        """
        db = (self.tmp / "case.sqlite3").as_posix()
        self._storage_yaml.write_text(
            f"metadata:\n  backend: sqlite\n  sqlite_path: {db}\n",
            encoding="utf-8",
        )
        return self._storage_yaml

    def cleanup(self) -> None:
        """Remove the owned temp directory; a no-op for an injected directory."""
        if self._owns_tmp:
            shutil.rmtree(self.tmp, ignore_errors=True)


# ---------------------------------------------------------------------------
# Adapter base class
# ---------------------------------------------------------------------------


class Adapter(ABC):
    """Base class every transport shim implements.

    A fresh adapter is constructed per case by the factory passed to
    :func:`build_suite`. :func:`run_case` calls :meth:`begin_case`, then
    :meth:`send` once per step (and once per job poll), then :meth:`end_case` in
    a ``finally`` block so resources are always released.

    :ivar transport: One id from :data:`TRANSPORT_CAPS`; its capabilities drive
        the id / raw / options skip decisions.
    """

    transport: str = ""

    def begin_case(self, case: Dict[str, Any], ctx: CaseContext) -> Optional[str]:
        """Prepare to run *case*; return ``None`` to run or a reason to skip.

        The default does nothing (run). Override to spawn a process, wait for
        readiness, or report that this case cannot run on this transport.

        :param case: The case about to run.
        :param ctx: The per-case context (temp dir, backend, variables).
        :return: ``None`` to proceed, or a non-empty reason string to skip the
            case (surfaced through ``unittest`` ``skipTest``).
        """
        return None

    @abstractmethod
    def send(
        self, step: Dict[str, Any], request_text: str, ctx: CaseContext
    ) -> Dict[str, Any]:
        """Send one request and return the response envelope (with status).

        The call **must** be timeout-bounded: a hung transport has to fail the
        case by timeout, never block CTest forever (use :func:`spawn` /
        :func:`terminate_and_reap`, and a bounded ``recv``).

        The return shape carries the transport status so :func:`run_case` can
        assert it:

        * ``direct`` / ``stdio`` / ``tui`` (no separate status channel): return
          the plain envelope ``dict``.
        * ``rest_generic`` / ``rest_resource``: return
          ``{"envelope": <dict>, "http_status": int}``.
        * ``cli_cmd``: return ``{"envelope": <dict>, "cli_exit": int}``.

        A real envelope never carries a top-level ``envelope`` key (N5), so the
        wrapper is detected unambiguously. Omitting the status on a transport
        that requires one is a finding. A wrapper may additionally carry an
        optional ``"carries_id": bool`` that overrides the transport's ``caps.id``
        for that one step (used by a multi-path adapter whose steps take different
        wire paths - e.g. ``rest_resource`` echoes the request id on a
        generic-fallback step but not on a synthesized resource-route envelope);
        when absent the transport default applies.

        :param step: The step dict after ``${var}`` / ``${tmp}`` substitution
            (so transport directives like ``rest_resource`` are already expanded).
        :param request_text: The substituted request serialized as text - a JSON
            envelope for a ``request`` step, or the verbatim string for a
            ``raw_request`` step.
        :param ctx: The per-case context.
        :return: The response envelope, or a ``{"envelope", "http_status" |
            "cli_exit"}`` wrapper for the transports that have a status channel.
        """
        raise NotImplementedError

    def end_case(self) -> None:
        """Release everything :meth:`begin_case` / :meth:`send` acquired.

        The default does nothing. Overrides must kill and reap every spawned
        process with a timeout; :func:`run_case` always calls this, even on
        failure or skip.
        """
        return None


# ---------------------------------------------------------------------------
# Transport-status assertion (enforced by run_case)
# ---------------------------------------------------------------------------


def _error_code_of(expect_block: Any) -> Optional[str]:
    """Return the ``error.code`` of an ``expect`` block, or ``None`` on success."""
    if isinstance(expect_block, dict) and expect_block.get("ok") is False:
        error = expect_block.get("error")
        if isinstance(error, dict):
            code = error.get("code")
            if isinstance(code, str):
                return code
    return None


def _status_from_expect(
    expect_block: Any, step: Dict[str, Any], channel: str
) -> int:
    """Return the transport status *channel* expects for *expect_block*.

    :param expect_block: The ``expect`` or ``expect_accept`` block that drives
        the expected status.
    :param step: The step dict; an explicit ``http_status`` / ``cli_exit``
        overrides the table.
    :param channel: ``"http"`` or ``"cli"``.
    :return: The explicit override, the catalog status/exit for the expected
        error code, or the success value (200 / 0).
    """
    if channel == "http":
        if isinstance(step.get("http_status"), int):
            return step["http_status"]
        code = _error_code_of(expect_block)
        return HTTP_STATUS.get(code, 500) if code is not None else 200
    if isinstance(step.get("cli_exit"), int):
        return step["cli_exit"]
    code = _error_code_of(expect_block)
    return CLI_EXIT.get(code, 4) if code is not None else 0


def expected_http_status(step: Dict[str, Any], success_status: int = 200) -> int:
    """Return the HTTP status a ``rest_*`` call should return for *step*.

    Public convenience for an adapter that wants to precompute the status;
    :func:`run_case` performs the same assertion itself.

    :param step: The step dict; an explicit ``http_status`` overrides the table.
    :param success_status: Status for an ``ok: true`` expectation (default 200).
    :return: The explicit override, the catalog status for the expected error
        code, or *success_status* when the step expects success.
    """
    status = _status_from_expect(step.get("expect", {}), step, "http")
    if _error_code_of(step.get("expect")) is None and not isinstance(
        step.get("http_status"), int
    ):
        return success_status
    return status


def expected_cli_exit(step: Dict[str, Any], success_exit: int = 0) -> int:
    """Return the ``cmd`` exit code a step should produce.

    Public convenience for an adapter that wants to precompute the exit code;
    :func:`run_case` performs the same assertion itself.

    :param step: The step dict; an explicit ``cli_exit`` overrides the table.
    :param success_exit: Exit code for an ``ok: true`` expectation (default 0).
    :return: The explicit override, the catalog exit code for the expected error
        code, or *success_exit* when the step expects success.
    """
    code = _status_from_expect(step.get("expect", {}), step, "cli")
    if _error_code_of(step.get("expect")) is None and not isinstance(
        step.get("cli_exit"), int
    ):
        return success_exit
    return code


def _unwrap_send(result: Any) -> Tuple[Any, Dict[str, Any]]:
    """Split an adapter ``send`` result into ``(envelope, status_wrapper)``.

    A wrapper is any dict carrying a top-level ``envelope`` key (which a real
    envelope never has, by N5); otherwise *result* is the envelope itself and
    the wrapper is empty.

    :param result: The value returned by :meth:`Adapter.send`.
    :return: ``(envelope, wrapper)`` where *wrapper* may hold ``http_status`` /
        ``cli_exit``.
    """
    if isinstance(result, dict) and "envelope" in result:
        return result.get("envelope"), result
    return result, {}


def _status_findings(
    wrapper: Dict[str, Any],
    transport: str,
    expect_block: Any,
    step: Dict[str, Any],
    label: str,
) -> List[str]:
    """Assert the transport status for a step; return findings (``[]`` on pass).

    For a transport with a status channel the wrapper must carry the status and
    it must equal the expected one; a missing status is a finding so a shim that
    forgets it cannot pass silently. Transports without a channel are ignored.

    :param wrapper: The status wrapper from :func:`_unwrap_send`.
    :param transport: The adapter's transport id.
    :param expect_block: The ``expect`` / ``expect_accept`` driving the status.
    :param step: The step dict (for an explicit status override).
    :param label: Step label used to locate findings.
    :return: ``[]`` when the status is correct (or not required), else findings.
    """
    channel = _STATUS_CHANNEL.get(transport)
    if channel is None:
        return []
    key = "http_status" if channel == "http" else "cli_exit"
    actual = wrapper.get(key)
    if not isinstance(actual, int):
        return [
            f"{label}: {transport} send() omitted {key}; this transport must "
            "return its transport status alongside the envelope"
        ]
    expected = _status_from_expect(expect_block, step, channel)
    if actual != expected:
        return [f"{label}: {transport} {key} {actual} != expected {expected}"]
    return []


# ---------------------------------------------------------------------------
# Binary discovery, free ports and HTTP readiness
# ---------------------------------------------------------------------------


def find_binary() -> str:
    """Return the path to the ``url_shortener`` binary.

    Wraps ``cli_integration_common.find_binary`` so the conformance shims honour
    ``SIMPLE_HTTP_BIN`` and the same ``cmake-build*/`` (and ``.exe``) fallbacks
    as the existing CLI integration tests, with no second copy of that logic.

    :return: Absolute path to the executable.
    :raises RuntimeError: If no binary is found.
    """
    cli_dir = _REPO_ROOT / "tests" / "integration" / "cli"
    if str(cli_dir) not in sys.path:
        sys.path.insert(0, str(cli_dir))
    from cli_integration_common import find_binary as _find  # noqa: E402

    return _find()


#: Env-var name prefixes the ``url_shortener`` binary reads as behavioral
#: overrides (``getenv`` under ``src/``: ``SHORTENER_ALLOW_PRIVATE_TARGETS``,
#: ``SHORTENER_BASE_DOMAIN``, ``SHORTENER_GENERATED_SLUG_LENGTH``,
#: ``SHORTENER_DEFAULT_REDIRECT_TYPE``, ``SHORTENER_DEFAULT_EXPIRY_SECONDS``,
#: ``URL_SHORTENER_STORAGE_CONFIG``, ``URL_SHORTENER_ANALYTICS_HASH_SALT``, ...).
#: Stripping the whole prefix future-proofs against a new variable the binary
#: starts reading. The harness's own vars begin ``URLSHORTENER_`` (no underscore,
#: e.g. ``URLSHORTENER_CONFORMANCE_DIR``) or ``SIMPLE_HTTP_BIN`` and so survive.
_BINARY_ENV_PREFIXES = ("SHORTENER_", "URL_SHORTENER_")


def conformance_env() -> Dict[str, str]:
    """Return a child environment with every binary behavioral override removed.

    Builds on ``cli_integration_common.clean_env`` (which already drops
    ``URL_SHORTENER_STORAGE_CONFIG``) and additionally strips every variable
    whose name starts with a prefix in :data:`_BINARY_ENV_PREFIXES`, so a spawned
    server or CLI child reads only the flags the shim passes and never an ambient
    ``SHORTENER_ALLOW_PRIVATE_TARGETS`` / base domain / slug length / analytics
    salt. Every shim that spawns the binary passes this as the child ``env`` so a
    developer or CI environment cannot change a case's verdict (hermetic replay).

    :return: A fresh ``dict`` copy of ``os.environ`` with the storage-config var
        and all ``SHORTENER_`` / ``URL_SHORTENER_`` prefixed vars removed; the
        harness-only ``URLSHORTENER_*`` and ``SIMPLE_HTTP_BIN`` vars are kept.
    """
    cli_dir = _REPO_ROOT / "tests" / "integration" / "cli"
    if str(cli_dir) not in sys.path:
        sys.path.insert(0, str(cli_dir))
    from cli_integration_common import clean_env  # noqa: E402

    env = clean_env()
    for key in list(env):
        if any(key.startswith(prefix) for prefix in _BINARY_ENV_PREFIXES):
            env.pop(key, None)
    return env


def free_port() -> int:
    """Return a currently-free loopback TCP port.

    Binds port 0, reads the assigned port and closes the socket. There is an
    inherent race between closing and the caller re-binding, so callers that
    start a server should retry start up to 3 times on bind failure.

    :return: A port number that was free at call time.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_http_ready(port: int, timeout: float = 15.0) -> None:
    """Poll ``GET /healthz`` on loopback *port* until the server answers 200.

    :param port: The loopback port the server listens on.
    :param timeout: Maximum seconds to wait before giving up.
    :raises TimeoutError: If ``/healthz`` did not return 200 within *timeout*.
    """
    deadline = time.monotonic() + timeout
    last_error: Optional[str] = None
    while time.monotonic() < deadline:
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=1.0)
        try:
            conn.request("GET", "/healthz")
            response = conn.getresponse()
            response.read()
            if response.status == 200:
                return
            last_error = f"status {response.status}"
        except OSError as exc:
            last_error = str(exc)
        finally:
            conn.close()
        time.sleep(0.05)
    raise TimeoutError(
        f"server on 127.0.0.1:{port} not ready within {timeout:.1f}s "
        f"(last: {last_error})"
    )


# ---------------------------------------------------------------------------
# Safe process spawn / reap (shims 06-09 MUST use these)
# ---------------------------------------------------------------------------


def spawn(
    argv: Any,
    cwd: Optional[str] = None,
    env: Optional[Dict[str, str]] = None,
    stdin: Any = None,
    stdout: Any = None,
    stderr: Any = None,
) -> "subprocess.Popen[Any]":
    """Spawn a child process from an explicit argv list, never a shell.

    Every shim (06-09) that starts the binary MUST go through this helper so no
    adapter can accidentally pass ``shell=True`` or a string command line.

    :param argv: The argument vector; must be a list/tuple of ``str`` (the
        program path first). A string or a non-``str`` element is rejected.
    :param cwd: Working directory for the child (``None`` inherits).
    :param env: Full environment mapping for the child (``None`` inherits).
    :param stdin: Passed through to ``Popen`` (e.g. ``subprocess.PIPE``).
    :param stdout: Passed through to ``Popen``.
    :param stderr: Passed through to ``Popen``.
    :return: The started ``subprocess.Popen``.
    :raises TypeError: If *argv* is not a list/tuple of ``str``.
    """
    if isinstance(argv, (str, bytes)) or not isinstance(argv, (list, tuple)):
        raise TypeError("spawn() argv must be a list/tuple of str, not a string")
    if not all(isinstance(arg, str) for arg in argv):
        raise TypeError("spawn() argv elements must all be str")
    # shell is never True: argv is passed as a list, so no shell is involved.
    return subprocess.Popen(
        list(argv),
        cwd=cwd,
        env=env,
        stdin=stdin,
        stdout=stdout,
        stderr=stderr,
    )


def terminate_and_reap(
    proc: Optional["subprocess.Popen[Any]"], timeout: float = 5.0
) -> Optional[int]:
    """Stop and reap *proc*: terminate, wait, then kill, then wait.

    Idempotent and never raises on an already-exited process, so an adapter's
    ``end_case`` can call it on every path.

    :param proc: The process to stop, or ``None`` (a no-op).
    :param timeout: Seconds to wait after each of terminate and kill.
    :return: The process exit code, or ``None`` when *proc* is ``None``.
    """
    if proc is None:
        return None
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            try:
                proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                pass
    return proc.returncode


# ---------------------------------------------------------------------------
# Corpus loading
# ---------------------------------------------------------------------------


def _conformance_commands_dir() -> Path:
    """Return the corpus directory (``URLSHORTENER_CONFORMANCE_DIR`` or default)."""
    env = os.environ.get("URLSHORTENER_CONFORMANCE_DIR")
    base = Path(env) if env else _CONFORMANCE_DIR
    return base / "commands"


def load_corpus(directory: Path) -> List[Dict[str, Any]]:
    """Load every corpus case from *directory*, sorted and annotated.

    Files are read in sorted order; each case gets ``_suite`` and ``_file``
    injected (underscore-prefixed, so they never collide with schema keys) for
    reporting. The corpus is linted before replay, so a parse failure is
    exceptional; a ``RecursionError`` on pathologically nested JSON is turned
    into a ``ValueError`` rather than crashing the run.

    :param directory: A directory of ``*.json`` corpus files.
    :return: The flattened list of cases in file-then-order sequence.
    :raises ValueError: If a file is not valid JSON or is too deeply nested.
    """
    cases: List[Dict[str, Any]] = []
    for path in sorted(Path(directory).glob("*.json")):
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except RecursionError as exc:
            raise ValueError(f"{path}: JSON nesting too deep to parse") from exc
        except json.JSONDecodeError as exc:
            raise ValueError(f"{path}: invalid JSON: {exc}") from exc
        suite = doc.get("suite")
        for case in doc.get("cases", []):
            if not isinstance(case, dict):
                continue
            annotated = dict(case)
            annotated["_suite"] = suite
            annotated["_file"] = str(path)
            cases.append(annotated)
    return cases


# ---------------------------------------------------------------------------
# Case execution
# ---------------------------------------------------------------------------


def _skip_or_findings(
    case: Dict[str, Any], transport: str
) -> Tuple[str, Any]:
    """Resolve whether *case* runs, skips or fails on *transport*.

    Mirrors the C++ ``CaseRunner`` skip resolution. The order is: explicit
    ``transports.only`` / ``transports.skip``, then backend vs transport, then
    the ``raw_request`` capability (a documented capability-derived skip), then
    the ``options`` capability. A capability mismatch that the corpus did **not**
    declare in ``transports.skip`` is a failure, not a silent skip - the one
    exception is ``raw_request``, which is capability-derived and listed in the
    corpus README.

    :param case: The case dict.
    :param transport: The adapter's transport id.
    :return: ``("skip", reason)``, ``("fail", [findings])`` or ``("run", None)``.
    """
    caps = TRANSPORT_CAPS[transport]
    transports = case.get("transports") or {}
    only = transports.get("only")
    skip = transports.get("skip") or {}
    steps = [s for s in case.get("steps", []) if isinstance(s, dict)]

    if isinstance(only, list) and transport not in only:
        return ("skip", f"{transport} is excluded by transports.only")
    if transport in skip and isinstance(skip[transport], str):
        return ("skip", skip[transport])

    if case.get("backend", "sqlite") == "inmemory" and transport in OUT_OF_PROCESS_TRANSPORTS:
        return ("skip", f"{transport} cannot share an in-memory backend")

    needs_raw = any("raw_request" in s for s in steps)
    if needs_raw and not caps.raw:
        return (
            "skip",
            f"{transport} does not carry raw_request (capability-derived)",
        )

    needs_options = any(
        isinstance(s.get("request"), dict) and "options" in s["request"]
        for s in steps
    )
    if needs_options and not caps.options:
        return (
            "fail",
            [
                f"{transport} lacks the 'options' capability but the case uses "
                f"request.options and declares no transports.skip['{transport}'] "
                "entry (capability skips must be explicit in the corpus)"
            ],
        )

    return ("run", None)


def _normalize_actual(
    actual: Any,
    request: Optional[Dict[str, Any]],
    carries_id: bool,
    transport: str,
) -> Tuple[Dict[str, Any], List[str]]:
    """Run N1/N4/N5 normalization, honouring a transport's server-minted id shape.

    This is a thin wrapper over :func:`conformance_match.normalize_actual` that
    keeps the shared N4 vectors unchanged. When the request carried **no** id and
    the transport mints a non-``srv-<n>`` id (only ``cli_cmd``, which has no C++
    runner), the server id is validated against that transport's own pattern here
    rather than against N4's ``^srv-[0-9]+$``. Every other case - including id
    echo when the request carried an id - delegates unchanged.

    :param actual: The actual response envelope.
    :param request: The request envelope, or ``None`` for a raw-text replay.
    :param carries_id: Whether the actual response echoes the request id (the
        ``caps.id`` capability, or a per-step override - see
        :func:`_check_envelope`); drives the N4 echo vs ``srv-<n>`` check.
    :param transport: The adapter's transport id (selects the generated-id shape).
    :return: ``(normalized_envelope, findings)``; findings is ``[]`` when the id
        and top-level shape are valid, else one finding per violation.
    """
    pattern = _GENERATED_ID_PATTERN.get(transport)
    has_request_id = isinstance(request, dict) and "id" in request
    if (
        pattern is not None
        and not has_request_id
        and isinstance(actual, dict)
        and isinstance(actual.get("id"), str)
    ):
        actual_id = actual["id"]
        # Treat the already-validated id as an echo of itself so normalize_actual
        # applies N1/N5 and the sentinel without re-imposing the srv-<n> rule.
        normalized, findings = cm.normalize_actual(actual, {"id": actual_id}, True)
        if not re.fullmatch(pattern, actual_id):
            findings = list(findings) + [
                f"$.id: generated id {actual_id!r} does not match "
                f"^{pattern}$ (N4, {transport})"
            ]
        return normalized, findings
    return cm.normalize_actual(actual, request, carries_id)


def _check_envelope(
    expect: Dict[str, Any],
    actual: Any,
    request: Optional[Dict[str, Any]],
    caps: Caps,
    transport: str,
    variables: Dict[str, Any],
    label: str,
    carries_id: Optional[bool] = None,
) -> List[str]:
    """Normalize *actual* and match it against an ``expect`` / ``expect_accept``.

    Builds the expected top-level envelope (``v``, ``ok``, ``id`` sentinel and
    one of ``result`` / ``job`` / ``error``), runs N1/N4/N5 normalization and the
    structural matcher, and prefixes every finding with *label*.

    :param expect: The ``expect`` (or ``expect_accept``) block, already
        substituted.
    :param actual: The response envelope the adapter returned.
    :param request: The request envelope (for the N4 id-echo check), or ``None``.
    :param caps: The transport capabilities (``caps.id`` drives N4 by default).
    :param transport: The adapter's transport id (selects the server-minted id
        shape when the request carried no id).
    :param variables: Variable map consulted by ``<var:name>``.
    :param label: Step label used to locate findings.
    :param carries_id: Per-step override of the ``caps.id`` capability. ``None``
        uses the transport default; a bool forces the N4 echo check for this step
        regardless of the transport default. A multi-path adapter (``rest_resource``
        echoes the request id on its generic-fallback steps but not on its
        synthesized resource-route envelopes) sets it per step so each step is
        judged by the path it actually took.
    :return: ``[]`` when the envelope matches, else one finding per violation.
    """
    if not isinstance(actual, dict):
        return [f"{label}: adapter returned a non-object envelope: {actual!r}"]
    expected: Dict[str, Any] = {"v": 1, "ok": expect.get("ok"), "id": "<id>"}
    for key in ("result", "job", "error"):
        if key in expect:
            if key == "error" and isinstance(expect["error"], dict):
                expected["error"] = {"code": expect["error"].get("code")}
            else:
                expected[key] = expect[key]
    mode = expect.get("match", "exact")
    effective_id = caps.id if carries_id is None else carries_id
    try:
        normalized, findings = _normalize_actual(
            actual, request, effective_id, transport)
        reasons = cm.match(expected, normalized, mode, variables)
    except RecursionError:
        return [f"{label}: envelope nesting too deep to match"]
    return [f"{label}: {text}" for text in findings + reasons]


def _apply_save(
    step: Dict[str, Any],
    response: Dict[str, Any],
    variables: Dict[str, Any],
    label: str,
) -> List[str]:
    """Apply a step's ``save`` map against *response*, writing *variables*.

    :param step: The step dict (its ``save`` object maps var -> JSONPath).
    :param response: The response the paths read from.
    :param variables: The variable map to update in place.
    :param label: Step label used to locate findings.
    :return: ``[]`` on success, else one finding per failed path.
    """
    save = step.get("save")
    if not isinstance(save, dict):
        return []
    findings: List[str] = []
    for var, path in save.items():
        try:
            variables[var] = cm.jsonpath_get(response, path)
        except (ValueError, KeyError, IndexError) as exc:
            findings.append(f"{label}: save {var!r} path {path!r} failed: {exc}")
    return findings


def _job_state(response: Any) -> Optional[str]:
    """Return the ``result.state`` of a ``job.get`` response, or ``None``."""
    if isinstance(response, dict):
        record = response.get("result")
        if isinstance(record, dict):
            return record.get("state")
    return None


def _project_terminal_job(response: Dict[str, Any]) -> Dict[str, Any]:
    """Project a terminal ``job.get`` response into an outcome envelope.

    A terminal ``job.get`` carries the job record under ``result`` with the
    command's own result nested at ``result.result`` (succeeded) or an error at
    ``result.error`` (failed / cancelled). The corpus ``expect`` compares against
    that command outcome, so this re-expresses the record as a flat
    ``{v, id, ok, result|error}`` envelope.

    :param response: The full terminal ``job.get`` response envelope.
    :return: The projected outcome envelope.
    """
    record = response.get("result") if isinstance(response, dict) else None
    record = record if isinstance(record, dict) else {}
    state = record.get("state")
    outcome: Dict[str, Any] = {
        "v": response.get("v", 1),
        "ok": state == "succeeded",
    }
    if "id" in response:
        outcome["id"] = response["id"]
    if state == "succeeded":
        outcome["result"] = record.get("result", {})
    else:
        error = record.get("error")
        outcome["error"] = error if isinstance(error, dict) else {}
    return outcome


def _poll_job(
    adapter: Adapter,
    await_spec: Dict[str, Any],
    ctx: CaseContext,
) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
    """Poll ``job.get`` for the saved job id until terminal or timeout.

    :param adapter: The adapter to send ``job.get`` through.
    :param await_spec: The ``await_job`` object (``var`` and ``timeout_ms``).
    :param ctx: The per-case context (its variables hold the saved job id).
    :return: ``(final_response, finding)`` - *finding* is ``None`` on a terminal
        state, else a timeout/poll finding; *final_response* is the last
        ``job.get`` response seen (or ``None`` if the id was never saved).
    """
    var = await_spec.get("var")
    if var not in ctx.variables:
        return None, f"await_job references unsaved variable {var!r}"
    job_id = ctx.variables[var]
    requested_ms = await_spec.get("timeout_ms", 5000)
    if not isinstance(requested_ms, int) or requested_ms < 0:
        requested_ms = 5000
    timeout_ms = min(requested_ms, _MAX_AWAIT_TIMEOUT_MS)
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    request = {"v": 1, "command": "job.get", "args": {"id": job_id}}
    request_text = json.dumps(request, ensure_ascii=False)
    step_stub = {"request": request}
    last: Optional[Dict[str, Any]] = None
    while True:
        try:
            result = adapter.send(step_stub, request_text, ctx)
        except Exception as exc:  # noqa: BLE001 - surface any adapter fault
            return last, f"job.get poll raised {type(exc).__name__}: {exc}"
        response, _ = _unwrap_send(result)
        last = response if isinstance(response, dict) else last
        state = _job_state(response)
        if state in _TERMINAL_JOB_STATES:
            return response, None
        if time.monotonic() >= deadline:
            return response, (
                f"job did not reach a terminal state within {timeout_ms} ms "
                f"(last state {state!r})"
            )
        time.sleep(_JOB_POLL_INTERVAL_S)


def _run_step(
    step: Dict[str, Any],
    label: str,
    adapter: Adapter,
    ctx: CaseContext,
    caps: Caps,
) -> List[str]:
    """Execute one step and return its findings (``[]`` when it passes)."""
    variables = ctx.variables
    variables["tmp"] = str(ctx.tmp)
    try:
        if "request" in step:
            request = cm.substitute(step["request"], variables)
            request_text = json.dumps(request, ensure_ascii=False)
        else:
            request = None
            request_text = cm.substitute(step.get("raw_request", ""), variables)
        sub_step = cm.substitute(step, variables)
    except ValueError as exc:
        return [f"{label}: variable substitution failed: {exc}"]
    except RecursionError:
        return [f"{label}: request nesting too deep to substitute"]

    transport = adapter.transport
    try:
        result = adapter.send(sub_step, request_text, ctx)
    except Exception as exc:  # noqa: BLE001 - surface any adapter fault
        return [f"{label}: adapter.send raised {type(exc).__name__}: {exc}"]
    actual, wrapper = _unwrap_send(result)
    # Optional per-step id-capability override: a multi-path adapter reports,
    # per step, whether the response echoed the request id (e.g. rest_resource's
    # generic-fallback steps do, its resource-route steps do not). ``None`` keeps
    # the transport default.
    carries_id = wrapper.get("carries_id")
    if not isinstance(carries_id, bool):
        carries_id = None

    if "await_job" not in step:
        try:
            expect = cm.substitute(step.get("expect", {}), variables)
        except ValueError as exc:
            return [f"{label}: expect substitution failed: {exc}"]
        findings = _status_findings(wrapper, transport, step.get("expect", {}), step, label)
        findings += _check_envelope(
            expect, actual, request, caps, transport, variables, label, carries_id
        )
        if findings:
            return findings
        return _apply_save(step, actual, variables, label)

    # Job path: check the immediate accept envelope, save the job id from it,
    # poll to a terminal state, then check expect against the projected outcome.
    findings: List[str] = []
    try:
        accept = cm.substitute(step.get("expect_accept", {}), variables)
    except ValueError as exc:
        return [f"{label}: expect_accept substitution failed: {exc}"]
    findings += _status_findings(
        wrapper, transport, step.get("expect_accept", {}), step, f"{label} (accept)"
    )
    findings += _check_envelope(
        accept, actual, request, caps, transport, variables, f"{label} (accept)",
        carries_id
    )
    findings += _apply_save(step, actual, variables, label)
    if findings:
        return findings

    terminal, poll_finding = _poll_job(adapter, step["await_job"], ctx)
    if poll_finding is not None:
        findings.append(f"{label}: {poll_finding}")
    if isinstance(terminal, dict):
        try:
            expect = cm.substitute(step.get("expect", {}), variables)
        except ValueError as exc:
            findings.append(f"{label}: expect substitution failed: {exc}")
            return findings
        outcome = _project_terminal_job(terminal)
        findings += _check_envelope(
            expect, outcome, None, caps, transport, variables, label
        )
    return findings


def _execute_steps(
    case: Dict[str, Any], adapter: Adapter, ctx: CaseContext
) -> List[str]:
    """Run every step in order, stopping at the first that fails."""
    caps = TRANSPORT_CAPS[adapter.transport]
    for index, step in enumerate(case.get("steps", [])):
        if not isinstance(step, dict):
            return [f"step {index + 1}: not an object"]
        label = step.get("label") or f"step {index + 1}"
        try:
            findings = _run_step(step, label, adapter, ctx, caps)
        except RecursionError:
            return [f"{label}: excessive JSON nesting"]
        if findings:
            return findings
    return []


def run_case(
    case: Dict[str, Any], adapter: Adapter, tmp_override: Optional[str] = None
) -> List[str]:
    """Replay one corpus *case* through *adapter* and return the findings.

    Mirrors the C++ ``CaseRunner``: skip resolution, ``${tmp}`` / ``${var}``
    substitution, ``save``, ``await_job`` polling and ``expect`` matching. The
    per-case temp directory is always cleaned up (``finally``), and
    :meth:`Adapter.end_case` is always called.

    :param case: The case dict (as produced by :func:`load_corpus`).
    :param adapter: The transport adapter to replay the case through.
    :param tmp_override: Testing hook forwarded to :class:`CaseContext`; leave
        ``None`` in production so a fresh owned temp dir is created.
    :return: ``[]`` when the case passes, else one finding per violation.
    :raises unittest.SkipTest: When the case is skipped (explicit
        ``transports`` directive, a capability-derived ``raw_request`` skip, or
        an adapter ``begin_case`` reason).
    """
    transport = adapter.transport
    if transport not in TRANSPORT_CAPS:
        raise ValueError(f"unknown transport {transport!r}")

    decision, payload = _skip_or_findings(case, transport)
    if decision == "skip":
        raise unittest.SkipTest(payload)
    if decision == "fail":
        return payload

    ctx = CaseContext(case, tmp_dir=tmp_override)
    try:
        try:
            ctx.materialize_files(case)
        except ValueError as exc:
            return [f"files materialization rejected: {exc}"]
        reason = adapter.begin_case(case, ctx)
        if reason is not None:
            raise unittest.SkipTest(reason)
        return _execute_steps(case, adapter, ctx)
    finally:
        try:
            adapter.end_case()
        finally:
            ctx.cleanup()


# ---------------------------------------------------------------------------
# unittest wiring and the coverage summary
# ---------------------------------------------------------------------------


class _SummaryCollector:
    """Accumulates per-case outcomes and writes the coverage summary JSON.

    :ivar transport: The transport id these records belong to.
    :ivar records: One dict per case (``name``, ``command``, ``suite``,
        ``status`` of ``pass`` / ``fail`` / ``skip``, and an optional ``reason``).
    """

    def __init__(self, transport: str) -> None:
        self.transport = transport
        self.records: List[Dict[str, Any]] = []

    def record(
        self, case: Dict[str, Any], status: str, reason: Optional[str]
    ) -> None:
        """Append one case outcome.

        :param case: The case that ran.
        :param status: ``"pass"``, ``"fail"`` or ``"skip"``.
        :param reason: A short reason for a skip/fail, else ``None``.
        """
        self.records.append(
            {
                "name": case.get("name"),
                "command": case.get("command"),
                "suite": case.get("_suite"),
                "status": status,
                "reason": reason,
            }
        )

    def write(self) -> Optional[Path]:
        """Write ``conformance_summary_<transport>.json`` if an out dir is set.

        The directory comes from ``$URLSHORTENER_CONFORMANCE_OUT`` (the coverage
        gate of subtask 15 reads it). When the variable is unset, nothing is
        written.

        :return: The path written, or ``None`` when no out directory is set.
        """
        out = os.environ.get("URLSHORTENER_CONFORMANCE_OUT")
        if not out:
            return None
        out_dir = Path(out)
        out_dir.mkdir(parents=True, exist_ok=True)
        path = out_dir / f"conformance_summary_{self.transport}.json"
        path.write_text(
            json.dumps(
                {"transport": self.transport, "cases": self.records}, indent=2
            ),
            encoding="utf-8",
        )
        return path


def _make_test_method(
    case: Dict[str, Any],
    adapter_factory: Callable[[], Adapter],
    collector: Optional[_SummaryCollector],
) -> Callable[[Any], None]:
    """Build one ``test_<case>`` method that replays *case*."""

    def test(self: Any) -> None:
        adapter = adapter_factory()
        try:
            findings = run_case(case, adapter)
        except unittest.SkipTest as skip:
            if collector is not None:
                collector.record(case, "skip", str(skip))
            raise
        if findings:
            if collector is not None:
                collector.record(case, "fail", "; ".join(findings))
            self.fail("\n".join(findings))
        if collector is not None:
            collector.record(case, "pass", None)

    test.__name__ = f"test_{case.get('name', 'unnamed')}"
    test.__doc__ = case.get("doc")
    return test


def build_suite(
    adapter_factory: Callable[[], Adapter],
    transport: str,
    collector: Optional[_SummaryCollector] = None,
    corpus_dir: Optional[Path] = None,
) -> unittest.TestSuite:
    """Build a suite with one ``test_<case>`` method per corpus case.

    :param adapter_factory: Zero-arg callable returning a fresh adapter per case.
    :param transport: The transport id (selects skip rules and the summary name).
    :param collector: Optional summary collector; :func:`main_for` supplies one.
    :param corpus_dir: Override the corpus directory (testing hook); defaults to
        ``$URLSHORTENER_CONFORMANCE_DIR/commands``.
    :return: A ``unittest.TestSuite`` over every case.
    """
    directory = corpus_dir if corpus_dir is not None else _conformance_commands_dir()
    cases = load_corpus(directory)

    class ConformanceCases(unittest.TestCase):
        """Generated per-transport conformance cases."""

    for case in cases:
        method = _make_test_method(case, adapter_factory, collector)
        setattr(ConformanceCases, method.__name__, method)

    return unittest.TestLoader().loadTestsFromTestCase(ConformanceCases)


def main_for(
    adapter_factory: Callable[[], Adapter], transport: str
) -> None:
    """Run every corpus case for *transport* and write the coverage summary.

    Used as the ``__main__`` body of each shim. Exits non-zero on any failure so
    CTest marks the shim failed.

    :param adapter_factory: Zero-arg callable returning a fresh adapter per case.
    :param transport: The transport id.
    """
    collector = _SummaryCollector(transport)
    suite = build_suite(adapter_factory, transport, collector)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    collector.write()
    sys.exit(0 if result.wasSuccessful() else 1)
