#!/usr/bin/env python3
"""10_cli_cmd_corpus.py - replay the command corpus through ``url_shortener cmd``.

Each corpus step is run as a fresh ``url_shortener cmd <name> --json -`` child
process (one process per step), with cross-process state carried by a SQLite file
in the per-case temp dir. This proves the CLI transport returns the same response
envelope and the catalog's exit code for every case (Task 07.0 subtask 06).

The shim obeys the mandatory harness contract (ruled at 07.0/05):

* every process is started with :func:`conformance_common.spawn` (list argv, never
  a shell) and reaped with :func:`conformance_common.terminate_and_reap` on every
  exit path;
* ``send`` returns ``{"envelope", "cli_exit"}`` so :func:`conformance_common.run_case`
  enforces the exit code against the catalog table;
* the ``user`` role is obtained only through the real ``--read-only`` flag, never
  through corpus ``args`` / ``options`` (threat brief ruling (c));
* command args are piped on ``cmd --json -`` stdin, whose read the server caps at
  ``max_request_body_bytes`` (corpus args stay far under that);
* stdlib only, and the file runs under ``python3 -I`` (CTest registers it so).

Envelope id (user ruling 2026-11-20): ``cli_cmd`` is echo-capable
(``TRANSPORT_CAPS["cli_cmd"].id = True``). When the corpus request carries an
``id`` the shim forwards it verbatim via ``cmd --id <id>`` and the harness checks
the response echoes it. When the request has no ``id`` the shim passes no
``--id``, so the CLI mints its own client id ``cli-<n>``
(``src/cli/cmd_dispatch.cpp:425``); the harness accepts ``^cli-[0-9]+$`` for this
transport (a Python-only rule, not an N4 vector change).

One as-built adjustment vs the subtask-06 pseudocode, documented at the call
site: ``--storage-config`` is appended **after** the command, because ``cmd`` is
recognised only as ``argv[1]`` (``cli_parser.cpp:1104``) and
``extract_storage_config_flag`` strips the selector from any position.
"""

from __future__ import annotations

import json
import socket
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

#: Default HTTP port a server would bind; ``cmd`` must never start a listener, so
#: the no-listener probe checks this after every case (success criterion 3).
_NO_LISTENER_PORT = 8000

#: Short connect timeout for the no-listener probe (mirrors
#: ``tests/e2e/scripts/sections/13_cli_no_server_socket.sh``).
_PROBE_TIMEOUT_S = 0.5


def _listening(port: int, host: str = "127.0.0.1") -> bool:
    """Return ``True`` if a loopback TCP connect to *host:port* succeeds.

    :param port: The TCP port to probe.
    :param host: The loopback host (default ``127.0.0.1``).
    :return: ``True`` when something accepts the connection, else ``False``.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(_PROBE_TIMEOUT_S)
        try:
            sock.connect((host, port))
            return True
        except (ConnectionRefusedError, OSError, TimeoutError):
            return False


class CliCmdAdapter(cc.Adapter):
    """Replay a corpus step through a one-shot ``url_shortener cmd`` process.

    A fresh adapter is built per case. :meth:`begin_case` materializes the SQLite
    ``--storage-config`` document so consecutive processes share state; each
    :meth:`send` spawns one child, pipes the step's args on stdin and returns the
    printed envelope together with the process exit code; :meth:`end_case` proves
    no listener was left behind.

    :ivar transport: Fixed ``"cli_cmd"`` transport id (selects the capability and
        skip rules in :data:`conformance_common.TRANSPORT_CAPS`).
    """

    transport = "cli_cmd"

    def __init__(self) -> None:
        """Initialise per-case state (caller role default, binary, probe state)."""
        self._caller = "admin"
        self._binary = ""
        # Conservative default: if begin_case never measured the port (e.g. a
        # skipped case), end_case must not attribute a pre-existing listener.
        self._port_preopen = True

    def begin_case(self, case: Dict[str, Any], ctx: cc.CaseContext) -> Any:
        """Prepare the case, or skip it when it cannot run one-shot.

        :param case: The case about to run (``backend`` and ``caller`` are read).
        :param ctx: The per-case context; its SQLite config is written here so the
            ``--storage-config`` argv the context built points at a real file.
        :return: A skip reason string when the case uses an in-memory backend a
            one-shot CLI cannot share, else ``None`` to run.
        """
        # Snapshot the listener state before any child runs so end_case only
        # flags a listener that this case's processes created.
        self._port_preopen = _listening(_NO_LISTENER_PORT)
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
        :raises AssertionError: If the command name begins with ``-`` (argument
            injection, CWE-88), the process times out, or it exits with stdout
            that is not a single JSON object (reported with argv/stdout/stderr;
            argv carries no secrets by construction - args travel on stdin).
        """
        req = json.loads(request_text)
        command = req["command"]
        if isinstance(command, str) and command.startswith("-"):
            # Refuse a command that could be mistaken for a flag (CWE-88) before
            # argv is built; a real command name is a dotted <group>.<verb>.
            raise AssertionError(
                f"refusing command name that starts with '-': {command!r}"
            )
        argv = [self._binary, "cmd", command, *ctx.storage_args, "--json", "-"]
        request_id = req.get("id")
        if isinstance(request_id, str) and request_id:
            # Echo path: forward the correlation id so the response repeats it.
            argv += ["--id", request_id]
        if step.get("caller", self._caller) == "user":
            argv.append("--read-only")
        options = req.get("options") or {}
        if options.get("dry_run"):
            argv.append("--dry-run")
        if options.get("async"):
            argv.append("--async")

        stdin_bytes = json.dumps(req.get("args", {})).encode("utf-8")
        proc = cc.spawn(argv, cwd=str(ctx.tmp), env=cc.conformance_env(),
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE)
        try:
            out, err = proc.communicate(input=stdin_bytes,
                                        timeout=_SEND_TIMEOUT_S)
        except subprocess.TimeoutExpired as exc:
            raise AssertionError(f"cmd did not exit within "
                                 f"{_SEND_TIMEOUT_S:.0f}s: argv={argv}") from exc
        finally:
            # Reap on every path out of communicate (timeout or any other
            # exception), not just the handled one; terminate_and_reap is a
            # no-op on an already-exited child.
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

    def end_case(self) -> None:
        """Assert the case left no HTTP listener behind (success criterion 3).

        ``cmd`` reserves no port, so only the default server port is probed. A
        listener that was already up before the case began (an external server)
        is not attributed; one that appeared during the case is a failure.

        :raises AssertionError: If a listener appeared on the default port while
            this case ran.
        """
        if not self._port_preopen and _listening(_NO_LISTENER_PORT):
            raise AssertionError(
                f"cmd left a listener on 127.0.0.1:{_NO_LISTENER_PORT}; a "
                "one-shot CLI command must never start the HTTP server"
            )


if __name__ == "__main__":
    cc.main_for(CliCmdAdapter, "cli_cmd")
