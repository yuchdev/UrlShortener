#!/usr/bin/env python3
"""13_rest_generic_corpus.py - replay the command corpus over POST /api/v1/commands.

Runs every corpus case (``tests/conformance/commands/*.json``) through a real
``url_shortener`` HTTP server and the generic command endpoint
``POST /api/v1/commands`` (catalog section 2; Task 07.0 subtask 08). It is the
``rest_generic`` sibling of the ``cli_cmd`` / ``stdio`` corpus shims (06, 07) and
shares its server/client plumbing with the resource-route shim (09) through
:mod:`conformance_rest`.

Harness contract (ruled at 07.0/05) honoured here:

  * :meth:`RestGenericAdapter.send` returns ``{"envelope", "http_status"}`` so
    :func:`conformance_common.run_case` enforces the HTTP status against the
    catalog error table for every step.
  * every server is started with :func:`conformance_common.spawn` (list argv,
    never a shell) via :class:`conformance_rest.ServerProcess` and reaped with
    :func:`conformance_common.terminate_and_reap`; every HTTP call is
    timeout-bounded and targets only ``127.0.0.1``.

Security (threat brief ruling (c)):

  * one server per role - a ``--command-api-role admin`` server for ``admin``
    steps and a separate ``--command-api-role user`` server for ``user`` steps,
    both over the one SQLite case file; the role comes only from the operator
    flag, never from corpus ``args`` / ``options``.
  * file paths stay default-deny (no ``--path-root`` / ``--no-file-paths``), so a
    ``path`` argument returns ``unsupported``.
  * ``inmemory`` cases never reach this adapter: :func:`conformance_common.run_case`
    skips every out-of-process transport for an in-memory backend (lint rule 10).

The module is Python standard library only and runs under ``python3 -I``; the
sibling imports are resolved from ``__file__``.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Dict, Optional

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402
import conformance_rest as cr  # noqa: E402

#: Max characters of a response body echoed into an assertion message, so a
#: failure stays readable even if a server returns an unexpectedly large body.
_RAW_SNIPPET_CHARS = 500


def _snippet(text: str) -> str:
    """Return *text* truncated to :data:`_RAW_SNIPPET_CHARS` with an elision tag."""
    if len(text) <= _RAW_SNIPPET_CHARS:
        return text
    return f"{text[:_RAW_SNIPPET_CHARS]}... (+{len(text) - _RAW_SNIPPET_CHARS} chars)"


class RestGenericAdapter(cc.Adapter):
    """Replay a corpus case through one HTTP server per role via POST /api/v1/commands.

    One adapter is built per case. :meth:`begin_case` records the backend argv and
    default role; :meth:`send` lazily spawns the server matching each step's
    effective role, POSTs the envelope and returns the response with its HTTP
    status; :meth:`end_case` reaps every server started.

    :ivar transport: Always ``"rest_generic"`` (selects the id/raw/options caps
        and the HTTP status channel in :data:`conformance_common.TRANSPORT_CAPS`).
    """

    transport = "rest_generic"

    def __init__(self) -> None:
        """Create an idle adapter; no server is spawned until the first step."""
        self._binary = str(Path(cc.find_binary()).resolve())
        self._servers: Dict[str, cr.ServerProcess] = {}
        self._ports: Dict[str, int] = {}
        self._client = cr.RestClient(self._ports)
        self._storage_args: list = []
        self._case_caller = "admin"
        self._cwd: Optional[str] = None
        self._log_dir: Optional[Path] = None

    def begin_case(self, case: Dict[str, Any], ctx: cc.CaseContext) -> Optional[str]:
        """Materialise the backend and record the default role.

        :param case: The case about to run (its ``caller`` is the default role).
        :param ctx: The per-case context; for a sqlite case the storage YAML is
            written so every role's server sees the same database file.
        :return: Always ``None``; inmemory cases are skipped upstream.
        """
        self._case_caller = case.get("caller", "admin")
        self._cwd = str(ctx.tmp)
        self._log_dir = ctx.tmp
        if ctx.backend == "sqlite":
            ctx.storage_config_path()
        self._storage_args = list(ctx.storage_args)
        return None

    def _server_for(self, role: str) -> None:
        """Ensure the server for *role* is running, spawning it on first use.

        :param role: ``"admin"`` or ``"user"``; each role gets at most one server.
        """
        if role in self._servers:
            return
        log_path = (self._log_dir or Path(".")) / f"server_{role}.log"
        server = cr.ServerProcess(self._binary, self._storage_args, role,
                                  log_path, cwd=self._cwd,
                                  env=cc.conformance_env())
        server.start()
        self._servers[role] = server
        self._ports[role] = server.port

    def send(
        self, step: Dict[str, Any], request_text: str, ctx: cc.CaseContext
    ) -> Dict[str, Any]:
        """POST one envelope and return ``{"envelope", "http_status"}``.

        :param step: The substituted step; its ``caller`` (if any) overrides the
            case default role.
        :param request_text: The substituted request (a JSON envelope, or a
            verbatim ``raw_request``).
        :param ctx: The per-case context (unused; backend was wired in begin_case).
        :return: ``{"envelope": <parsed body>, "http_status": <status>}`` for
            :func:`conformance_common.run_case` to assert.
        :raises AssertionError: If the response is not ``application/json`` or its
            body is not a JSON object.
        """
        role = step.get("caller") or self._case_caller
        self._server_for(role)
        status, envelope, raw, content_type = self._client.post_command(
            role, request_text)
        media = content_type.split(";", 1)[0].strip().lower()
        assert media == "application/json", (
            f"rest_generic response Content-Type {content_type!r} is not JSON "
            f"(status {status}, body {_snippet(raw)!r})")
        assert envelope is not None, (
            f"rest_generic response body is not JSON (status {status}): "
            f"{_snippet(raw)!r}")
        return {"envelope": envelope, "http_status": status}

    def end_case(self) -> None:
        """Reap every server started for this case (always, even on failure)."""
        for server in self._servers.values():
            server.stop()
        self._servers.clear()
        self._ports.clear()


if __name__ == "__main__":
    cc.main_for(RestGenericAdapter, "rest_generic")
