#!/usr/bin/env python3
"""11_stdio_corpus.py - replay the command conformance corpus over serve-stdio.

Runs every corpus case (``tests/conformance/commands/*.json``) through one
long-lived ``url_shortener serve-stdio`` process per case: each step writes one
request envelope as an NDJSON stdin line and reads one response envelope back
from stdout (catalog section 2, contract C2). It is the ``stdio`` sibling of the
``cli_cmd`` / ``rest_*`` corpus shims (subtasks 06, 08, 09).

Harness contract (ruled at the 07.0/05 review) honoured here:

  * stdio has no separate transport-status channel, so :meth:`StdioAdapter.send`
    returns the plain response envelope (``_STATUS_CHANNEL["stdio"] is None``).
  * the child is spawned with :func:`conformance_common.spawn` and reaped with
    :func:`conformance_common.terminate_and_reap` (via :class:`StdioProc`), and
    every read is timeout-bounded.
  * a ``user`` step runs on a *second* ``serve-stdio --read-only`` process - the
    role comes only from the real ``--read-only`` flag, never from corpus
    ``args`` / ``options`` (threat brief ruling (c)). The two processes share the
    one SQLite case file, so mixed-role cases are ``backend: sqlite`` only.
  * ``inmemory`` cases never reach this adapter: ``conformance_common.run_case``
    skips every out-of-process transport for an in-memory backend (lint rule 10).

The adapter honours the serve-stdio file-path default: no path flags are passed,
so a write-capable operator keeps the local default (``--no-file-paths`` /
``--path-root`` remain a deliberate operator opt-in, T-95 / T-107).

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
_CLI_DIR = _THIS.parents[3] / "tests" / "integration" / "cli"
if str(_CLI_DIR) not in sys.path:
    sys.path.insert(0, str(_CLI_DIR))

import conformance_common as cc  # noqa: E402
from cli_integration_common import clean_env  # noqa: E402
from stdio_client import StdioProc  # noqa: E402


class StdioAdapter(cc.Adapter):
    """Replay a corpus case over one (or, for mixed roles, two) stdio processes.

    One adapter is built per case. :meth:`begin_case` records the case's backend
    argv and default role; :meth:`send` lazily spawns the process matching each
    step's effective role; :meth:`end_case` reaps every process started.

    :ivar transport: Always ``"stdio"`` (selects the id/raw/options caps and the
        absent transport-status channel).
    """

    transport = "stdio"

    def __init__(self) -> None:
        """Create an idle adapter; no process is spawned until the first step."""
        self._binary = cc.find_binary()
        self._procs: Dict[str, StdioProc] = {}
        self._storage_args: list = []
        self._case_caller = "admin"
        self._cwd: Optional[str] = None

    def begin_case(self, case: Dict[str, Any], ctx: cc.CaseContext) -> Optional[str]:
        """Materialise the case backend and record its default role.

        :param case: The case about to run (its ``caller`` is the default role).
        :param ctx: The per-case context; for a sqlite case the storage YAML is
            written here so every role's process sees the same database file.
        :return: Always ``None`` (run); inmemory cases are skipped upstream by
            :func:`conformance_common.run_case`.
        """
        self._case_caller = case.get("caller", "admin")
        self._cwd = str(ctx.tmp)
        if ctx.backend == "sqlite":
            ctx.storage_config_path()
        self._storage_args = list(ctx.storage_args)
        return None

    def _session_for(self, role: str) -> StdioProc:
        """Return the live process for *role*, spawning it on first use.

        :param role: ``"admin"`` (write-capable) or ``"user"`` (started with the
            real ``--read-only`` flag). Each role gets at most one process.
        :return: The :class:`StdioProc` bound to *role*.
        """
        proc = self._procs.get(role)
        if proc is None:
            args = ["serve-stdio", *self._storage_args]
            if role == "user":
                args.append("--read-only")
            proc = StdioProc(self._binary, *args, cwd=self._cwd, env=clean_env())
            self._procs[role] = proc
        return proc

    def send(
        self, step: Dict[str, Any], request_text: str, ctx: cc.CaseContext
    ) -> Dict[str, Any]:
        """Write one NDJSON request line and return the parsed response envelope.

        :param step: The substituted step dict; its ``caller`` (if any) overrides
            the case default role.
        :param request_text: The single-line request (envelope JSON or a verbatim
            ``raw_request``); an embedded newline would break NDJSON framing and
            is a framing test, not a corpus case.
        :param ctx: The per-case context (unused here; backend was wired in
            :meth:`begin_case`).
        :return: The plain response envelope (stdio has no status wrapper).
        :raises AssertionError: *request_text* contains an embedded newline.
        """
        if "\n" in request_text:
            raise AssertionError(
                "stdio corpus request carries an embedded newline; a multi-line "
                "raw_request is a framing test (12_stdio_framing), not a case"
            )
        role = step.get("caller") or self._case_caller
        proc = self._session_for(role)
        proc.write_line(request_text)
        return proc.read_json(timeout=10.0)

    def end_case(self) -> None:
        """Reap every process started for this case (always, even on failure)."""
        for proc in self._procs.values():
            proc.stop()
        self._procs.clear()


if __name__ == "__main__":
    cc.main_for(StdioAdapter, "stdio")
