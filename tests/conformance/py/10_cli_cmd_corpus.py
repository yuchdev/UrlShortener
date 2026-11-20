#!/usr/bin/env python3
"""10_cli_cmd_corpus.py - replay the command corpus through ``url_shortener cmd``.

Each corpus step is run as a fresh ``url_shortener cmd <name> --json -`` child
process (one process per step), with cross-process state carried by a SQLite file
in the per-case temp dir. This proves the CLI transport returns the same response
envelope and the catalog's exit code for every case (Task 07.0 subtask 06).

The shim obeys the mandatory harness contract (ruled at 07.0/05):

* every process is started with :func:`conformance_common.spawn` (list argv, never
  a shell) and reaped with :func:`conformance_common.terminate_and_reap`;
* ``send`` returns ``{"envelope", "cli_exit"}`` so :func:`conformance_common.run_case`
  enforces the exit code against the catalog table;
* the ``user`` role is obtained only through the real ``--read-only`` flag, never
  through corpus ``args`` / ``options`` (threat brief ruling (c));
* command args are piped on ``cmd --json -`` stdin, whose read the server caps at
  ``max_request_body_bytes`` (corpus args stay far under that);
* stdlib only, and the file runs under ``python3 -I`` (CTest registers it so).

Two behaviours diverge from the subtask 06 pseudocode after inspecting the
as-built CLI (both documented at their call site):

1. ``cmd`` is recognised only as ``argv[1]`` (``cli_parser.cpp:1104``), so the
   ``--storage-config`` selector is appended **after** the command, where
   ``extract_storage_config_flag`` strips it from any position - not before
   ``cmd`` as the pseudocode showed.
2. ``cmd`` mints the envelope ``id`` as ``cli-1`` by default
   (``cmd_dispatch.cpp:425``), but the locked matcher's N4 rule requires a
   non-echoed id to match ``^srv-[0-9]+$`` (``cli_cmd`` has ``caps.id=False``).
   The shim therefore supplies a server-style ``--id srv-<n>`` per step, modelling
   "the server minted an id"; because ``caps.id=False`` the harness never compares
   it to a corpus request id.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402

#: Hard per-process wall clock; a hung ``cmd`` fails its step by timeout rather
#: than blocking CTest (the subtask budgets 30 s per process).
_SEND_TIMEOUT_S = 30.0


class CliCmdAdapter(cc.Adapter):
    """Replay a corpus step through a one-shot ``url_shortener cmd`` process.

    A fresh adapter is built per case. :meth:`begin_case` materializes the SQLite
    ``--storage-config`` document so consecutive processes share state; each
    :meth:`send` spawns one child, pipes the step's args on stdin and returns the
    printed envelope together with the process exit code.

    :ivar transport: Fixed ``"cli_cmd"`` transport id (selects the capability and
        skip rules in :data:`conformance_common.TRANSPORT_CAPS`).
    """

    transport = "cli_cmd"

    def __init__(self) -> None:
        """Initialise per-case state (caller role default and the id counter)."""
        self._caller = "admin"
        self._seq = 0
        self._binary = ""

    def begin_case(self, case: Dict[str, Any], ctx: cc.CaseContext) -> Any:
        """Prepare the case, or skip it when it cannot run one-shot.

        :param case: The case about to run (``backend`` and ``caller`` are read).
        :param ctx: The per-case context; its SQLite config is written here so the
            ``--storage-config`` argv the context built points at a real file.
        :return: A skip reason string when the case uses an in-memory backend a
            one-shot CLI cannot share, else ``None`` to run.
        """
        if case.get("backend") == "inmemory":
            return "one-shot CLI cannot share in-memory state"
        self._caller = case.get("caller", "admin")
        # Resolve to an absolute path once: send() runs with cwd=ctx.tmp, so a
        # relative SIMPLE_HTTP_BIN would otherwise not be found by the child.
        self._binary = str(Path(cc.find_binary()).resolve())
        if ctx.backend == "sqlite":
            ctx.storage_config_path()
        return None

    def send(
        self, step: Dict[str, Any], request_text: str, ctx: cc.CaseContext
    ) -> Dict[str, Any]:
        """Run one ``cmd`` process and return ``{"envelope", "cli_exit"}``.

        ``raw_request`` never reaches here (``cli_cmd`` has ``caps.raw=False`` so
        such steps are skipped), so *request_text* is always a JSON envelope.

        :param step: The substituted step (its effective ``caller`` selects the
            role, defaulting to the case role).
        :param request_text: The substituted request envelope as JSON text.
        :param ctx: The per-case context (temp dir, backend selector args).
        :return: ``{"envelope": <parsed stdout>, "cli_exit": <process code>}``.
        :raises AssertionError: If the process times out, or exits with stdout
            that is not a single JSON object (reported with argv/stdout/stderr;
            argv carries no secrets by construction - args travel on stdin).
        """
        req = json.loads(request_text)
        self._seq += 1
        argv = [self._binary, "cmd", req["command"], *ctx.storage_args,
                "--id", f"srv-{self._seq}", "--json", "-"]
        if step.get("caller", self._caller) == "user":
            argv.append("--read-only")
        options = req.get("options") or {}
        if options.get("dry_run"):
            argv.append("--dry-run")
        if options.get("async"):
            argv.append("--async")

        stdin_bytes = json.dumps(req.get("args", {})).encode("utf-8")
        proc = cc.spawn(argv, cwd=str(ctx.tmp), stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            out, err = proc.communicate(input=stdin_bytes,
                                        timeout=_SEND_TIMEOUT_S)
        except subprocess.TimeoutExpired as exc:
            cc.terminate_and_reap(proc)
            raise AssertionError(f"cmd did not exit within "
                                 f"{_SEND_TIMEOUT_S:.0f}s: argv={argv}") from exc
        exit_code = cc.terminate_and_reap(proc)
        text = out.decode("utf-8", "replace")
        try:
            envelope = json.loads(text)
        except json.JSONDecodeError as exc:
            raise AssertionError(
                f"cmd stdout is not one JSON object (exit {exit_code}): {exc}; "
                f"argv={argv} stdout={text!r} "
                f"stderr={err.decode('utf-8', 'replace')!r}"
            ) from exc
        return {"envelope": envelope, "cli_exit": exit_code}


if __name__ == "__main__":
    cc.main_for(CliCmdAdapter, "cli_cmd")
