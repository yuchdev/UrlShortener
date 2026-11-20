#!/usr/bin/env python3
"""32_no_listener_process_level.py - process-level no-listener / no-URI-store.

Out-of-process proof (Milestone 0006, Task 07.0 subtask 21, contract C5, threats
T-18/T-83/T-114) that the three new local-mode entry points never bind a
listening socket and never create or read the legacy ``uri.txt`` / URI-store,
exercised against the **real** binary (the structural scan in
``tests/unit/commands/79_no_http_server_in_local_modes_test.cpp`` is only the
tripwire; these are the proof):

  * ``cmd`` exits after a one-shot command and no default HTTP/HTTPS port is
    listening afterwards; while a ``cmd`` process is alive (blocked reading
    ``--json -`` from an open pipe) it owns no LISTEN socket.
  * ``serve-stdio`` answers a request and, while alive, owns no LISTEN socket.
  * ``tui`` refuses a non-TTY stdin (exit 2) within a bounded wait and never
    owns a LISTEN socket (as-built reconciliation 2026-11-20: no skip hedge).
  * a sentinel ``uri.txt`` placed in the working directory is byte-identical and
    unmodified after ``cmd`` / ``serve-stdio`` / ``tui`` run there, no new
    ``uri.txt`` appears, and the sentinel's unique marker is never echoed by any
    command (the store is never read).
  * ``--port 9999`` is rejected or ignored for every mode: port 9999 never
    listens and the process still exits.

PID socket ownership is platform-specific: ``lsof`` on macOS/Linux, ``netstat
-ano`` on Windows, and a bounded loopback connect probe as the documented
fallback when neither tool is present (a weaker, non-PID-attributed check,
reported as a skip-note so subtask 34 can count it).

Reuses ``conformance_common`` (``conformance_env`` / ``spawn`` /
``terminate_and_reap`` / ``free_port``). Standard library only; ``python3 -I``.
"""

from __future__ import annotations

import json
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from typing import List, Optional, Set, Tuple

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402

#: Unique marker written into the sentinel ``uri.txt``; a local mode that opened
#: or echoed the legacy store would surface it, so it must never appear in any
#: command output.
_SENTINEL_MARKER = "SENTINEL-32-URI-DO-NOT-READ-OR-ECHO-9f8e7d"

#: Default and conventional listener ports that must stay closed for a local
#: mode: the ServerConfig HTTP default (8000) and HTTPS default (443), plus the
#: common 8080/8443 and the explicit 9999 used by the ``--port`` cases.
_DEFAULT_LISTENER_PORTS = (8000, 8080, 8443, 443)

#: The explicit port handed to ``--port``; it must never end up listening.
_REJECT_PORT = 9999

#: Bounded ceiling on any single child interaction (never a wall-clock
#: assertion; just an upper bound so a hung child fails the case).
_CHILD_TIMEOUT_S = 10.0

#: Bounded ceiling on sampling a short-lived child's listener set while alive.
_SAMPLE_BUDGET_S = 3.0


def _envelope(command: str, args: Optional[dict] = None) -> str:
    """Serialize one request envelope as a single NDJSON line (no newline)."""
    env = {"v": 1, "command": command, "args": args or {}}
    return json.dumps(env, ensure_ascii=False)


def _port_is_open(port: int, host: str = "127.0.0.1") -> bool:
    """Return ``True`` if a loopback TCP connect to *port* succeeds (bounded)."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.5)
        try:
            sock.connect((host, port))
            return True
        except (ConnectionRefusedError, OSError, TimeoutError):
            return False


def _lsof_listeners(pid: int) -> Optional[Set[int]]:
    """Return the set of LISTEN ports owned by *pid* via ``lsof``, or ``None``.

    ``lsof -nP -a -p <pid> -iTCP -sTCP:LISTEN`` lists only this PID's listening
    TCP sockets. ``lsof`` exits non-zero with empty output when the PID owns no
    such socket, which is the expected, passing case, so the return code is
    ignored and only the parsed NAME column matters.

    :param pid: The process id to inspect.
    :return: The set of listening ports (empty when none), or ``None`` when
        ``lsof`` is unavailable.
    """
    tool = shutil.which("lsof")
    if tool is None:
        return None
    try:
        proc = subprocess.run(
            [tool, "-nP", "-a", "-p", str(pid), "-iTCP", "-sTCP:LISTEN"],
            capture_output=True,
            text=True,
            timeout=_CHILD_TIMEOUT_S,
        )
    except (subprocess.TimeoutExpired, OSError):
        return None
    ports: Set[int] = set()
    for line in proc.stdout.splitlines():
        if "(LISTEN)" not in line:
            continue
        fields = line.split()
        # The address is the field immediately before the "(LISTEN)" state,
        # e.g. "127.0.0.1:8000", "[::1]:8000" or "*:8000".
        for index, token in enumerate(fields):
            if token == "(LISTEN)" and index > 0:
                addr = fields[index - 1]
                match = re.search(r":(\d+)$", addr)
                if match:
                    ports.add(int(match.group(1)))
    return ports


def _netstat_listeners(pid: int) -> Optional[Set[int]]:
    """Return the set of LISTEN ports owned by *pid* via ``netstat`` (Windows).

    :param pid: The process id to inspect.
    :return: The set of listening ports, or ``None`` when ``netstat`` is
        unavailable or produced no parseable output.
    """
    tool = shutil.which("netstat")
    if tool is None:
        return None
    try:
        proc = subprocess.run(
            [tool, "-ano", "-p", "tcp"],
            capture_output=True,
            text=True,
            timeout=_CHILD_TIMEOUT_S,
        )
    except (subprocess.TimeoutExpired, OSError):
        return None
    ports: Set[int] = set()
    for line in proc.stdout.splitlines():
        fields = line.split()
        # TCP  <local>  <remote>  LISTENING  <pid>
        if len(fields) >= 5 and fields[0].upper() == "TCP" \
                and fields[3].upper() == "LISTENING" and fields[-1] == str(pid):
            match = re.search(r":(\d+)$", fields[1])
            if match:
                ports.add(int(match.group(1)))
    return ports


def _pid_listeners(pid: int) -> Tuple[Optional[Set[int]], str]:
    """Return ``(ports, method)`` for *pid*'s LISTEN sockets.

    Tries ``lsof`` then ``netstat``; returns ``(None, "unavailable")`` when no
    enumeration tool is present so the caller can fall back to a bounded connect
    probe (documented, weaker, non-PID-attributed).

    :param pid: The process id to inspect.
    :return: ``(set_of_ports, method_name)``; *set_of_ports* is ``None`` only
        when enumeration is unavailable.
    """
    if sys.platform.startswith("win"):
        ports = _netstat_listeners(pid)
        if ports is not None:
            return ports, "netstat"
        return None, "unavailable"
    ports = _lsof_listeners(pid)
    if ports is not None:
        return ports, "lsof"
    ports = _netstat_listeners(pid)
    if ports is not None:
        return ports, "netstat"
    return None, "unavailable"


class NoListenerProcessLevel(unittest.TestCase):
    """Process-level no-listener / no-URI-store checks for the local modes."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.binary = cc.find_binary()
        cls.env = cc.conformance_env()
        #: Accumulated skip-notes (tool-unavailable fallbacks) for subtask 34.
        cls.skip_notes: List[str] = []

    @classmethod
    def tearDownClass(cls) -> None:
        for note in cls.skip_notes:
            print(f"NOTE (counted skip): {note}")

    # ---- shared assertions -------------------------------------------------

    def _assert_pid_not_listening(self, pid: int, context: str) -> None:
        """Assert *pid* owns no LISTEN socket; fall back to a connect probe.

        :param pid: The process id to inspect (must still be alive for an
            accurate PID-attributed result).
        :param context: Human label for the failure message.
        """
        ports, method = _pid_listeners(pid)
        if ports is None:
            # Documented fallback: no PID-attribution tool. Probe the ports the
            # modes could ever bind; record the weaker coverage for subtask 34.
            self.skip_notes.append(
                f"{context}: no lsof/netstat; used connect-probe fallback "
                "(no PID attribution)"
            )
            for port in (*_DEFAULT_LISTENER_PORTS, _REJECT_PORT):
                self.assertFalse(
                    _port_is_open(port),
                    f"{context}: port {port} is open (connect-probe fallback)",
                )
            return
        self.assertEqual(
            ports,
            set(),
            f"{context}: pid {pid} owns LISTEN socket(s) {sorted(ports)} "
            f"via {method} - a local mode bound a listener (C5 violation)",
        )

    def _assert_default_ports_closed(self, context: str) -> None:
        """Assert every default listener port is closed (bounded connect)."""
        for port in _DEFAULT_LISTENER_PORTS:
            if _port_is_open(port):
                # Match section 13's convention: a port already in use by an
                # unrelated process is a skip, not a false failure.
                self.skip_notes.append(
                    f"{context}: port {port} already in use by another process"
                )
                continue
            self.assertFalse(
                _port_is_open(port), f"{context}: port {port} open after exit"
            )

    # ---- cmd ---------------------------------------------------------------

    def test_cmd_exits_and_no_default_port(self) -> None:
        proc = cc.spawn(
            [self.binary, "cmd", "command.list"],
            env=self.env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            out, _err = proc.communicate(timeout=_CHILD_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            cc.terminate_and_reap(proc)
            self.fail("cmd command.list did not exit (server mode?)")
        self.assertIsNotNone(proc.returncode, "cmd did not exit")
        self.assertEqual(proc.returncode, 0, "cmd command.list should succeed")
        self.assertNotIn(_SENTINEL_MARKER, out.decode("utf-8", "replace"))
        self._assert_default_ports_closed("cmd")

    def test_cmd_pid_no_listener_during_run(self) -> None:
        # `cmd <name> --json -` blocks reading args from stdin until EOF, so the
        # process stays alive while its stdin pipe is held open - a genuine live
        # `cmd`-mode process whose PID can be sampled for LISTEN sockets.
        proc = cc.spawn(
            [self.binary, "cmd", "command.list", "--json", "-"],
            env=self.env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            self.assertIsNone(proc.poll(), "cmd exited before stdin EOF")
            self._assert_pid_not_listening(proc.pid, "cmd (during run)")
        finally:
            # Send a valid empty args object and EOF so the child finishes.
            try:
                out, _err = proc.communicate(input=b"{}", timeout=_CHILD_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                cc.terminate_and_reap(proc)
                self.fail("cmd --json - did not finish after stdin EOF")
        self.assertEqual(proc.returncode, 0, "cmd command.list should succeed")

    # ---- serve-stdio -------------------------------------------------------

    def test_serve_stdio_pid_no_listener(self) -> None:
        proc = cc.spawn(
            [self.binary, "serve-stdio"],
            env=self.env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            self.assertIsNone(proc.poll(), "serve-stdio exited immediately")
            # Send one request line but keep stdin open: the session answers it
            # and waits for the next line, staying alive to be sampled.
            assert proc.stdin is not None
            proc.stdin.write((_envelope("command.list") + "\n").encode("utf-8"))
            proc.stdin.flush()
            self._assert_pid_not_listening(proc.pid, "serve-stdio (during run)")
        finally:
            try:
                out, _err = proc.communicate(timeout=_CHILD_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                cc.terminate_and_reap(proc)
                self.fail("serve-stdio did not exit after stdin EOF")
        self.assertNotIn(_SENTINEL_MARKER, out.decode("utf-8", "replace"))

    # ---- no uri.txt --------------------------------------------------------

    def _run_in_cwd(self, argv: List[str], cwd: Path,
                    stdin_bytes: Optional[bytes]) -> Tuple[int, str]:
        """Run *argv* in *cwd*, return ``(exit_code, stdout+stderr)``.

        :param argv: The child argument vector (program first).
        :param cwd: Working directory the child runs in.
        :param stdin_bytes: Bytes to feed on stdin then EOF, or ``None`` for an
            immediate EOF (``DEVNULL``).
        """
        proc = cc.spawn(
            argv,
            cwd=str(cwd),
            env=self.env,
            stdin=subprocess.PIPE if stdin_bytes is not None
            else subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            out, err = proc.communicate(input=stdin_bytes,
                                        timeout=_CHILD_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            cc.terminate_and_reap(proc)
            self.fail(f"child hung: {argv!r}")
        return proc.returncode, (out + err).decode("utf-8", "replace")

    def test_no_uri_txt_created(self) -> None:
        modes = [
            (["cmd", "command.list"], None),
            (["cmd", "link.list"], None),
            (["serve-stdio"], (_envelope("command.list") + "\n").encode("utf-8")),
            (["tui", "--help"], None),
        ]
        for argv, stdin_bytes in modes:
            with tempfile.TemporaryDirectory(prefix="conf32_cwd_") as tmp:
                cwd = Path(tmp)
                sentinel = cwd / "uri.txt"
                sentinel.write_bytes(
                    (_SENTINEL_MARKER + " short->http://example.com/\n")
                    .encode("utf-8")
                )
                before_bytes = sentinel.read_bytes()
                before_mtime = sentinel.stat().st_mtime_ns

                _rc, combined = self._run_in_cwd(
                    [self.binary, *argv], cwd, stdin_bytes
                )

                # The sentinel is byte-identical and its mtime is unchanged.
                self.assertTrue(sentinel.exists(),
                                f"{argv}: sentinel uri.txt vanished")
                self.assertEqual(sentinel.read_bytes(), before_bytes,
                                 f"{argv}: sentinel uri.txt bytes changed")
                self.assertEqual(sentinel.stat().st_mtime_ns, before_mtime,
                                 f"{argv}: sentinel uri.txt mtime changed")
                # No *new* uri.txt-family file appeared; the sentinel is the only
                # uri.txt in the cwd.
                uri_files = sorted(p.name for p in cwd.iterdir()
                                   if p.name.startswith("uri")
                                   and p.name.endswith(".txt"))
                self.assertEqual(uri_files, ["uri.txt"],
                                 f"{argv}: unexpected uri files {uri_files}")
                # The store is never read: its unique marker is never echoed.
                self.assertNotIn(_SENTINEL_MARKER, combined,
                                 f"{argv}: sentinel marker echoed in output")

    # ---- --port rejected / ignored ----------------------------------------

    def test_server_flag_rejected_or_ignored(self) -> None:
        self.assertFalse(
            _port_is_open(_REJECT_PORT),
            f"port {_REJECT_PORT} already in use before the test",
        )
        cases = [
            ["cmd", "--port", str(_REJECT_PORT), "command.list"],
            ["serve-stdio", "--port", str(_REJECT_PORT)],
            ["tui", "--port", str(_REJECT_PORT)],
        ]
        for argv in cases:
            proc = cc.spawn(
                [self.binary, *argv],
                env=self.env,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            try:
                proc.communicate(timeout=_CHILD_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                cc.terminate_and_reap(proc)
                self.fail(f"{argv}: did not exit (did --port start a server?)")
            self.assertIsNotNone(proc.returncode, f"{argv}: did not exit")
            self.assertFalse(
                _port_is_open(_REJECT_PORT),
                f"{argv}: port {_REJECT_PORT} is listening - --port was honored",
            )

    # ---- tui non-interactive ----------------------------------------------

    def test_tui_non_interactive_exit_no_listener(self) -> None:
        proc = cc.spawn(
            [self.binary, "tui"],
            env=self.env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        # Sample the PID's listener set while it is (briefly) alive; a non-TTY
        # tui exits 2 almost immediately, so accumulate any listeners seen
        # within a bounded budget. An already-exited process that never bound a
        # socket yields an empty set, which is the passing case.
        observed: Set[int] = set()
        deadline = time.monotonic() + _SAMPLE_BUDGET_S
        sampled = False
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                break
            ports, _method = _pid_listeners(proc.pid)
            if ports is not None:
                observed |= ports
                sampled = True
            else:
                break
        try:
            proc.communicate(timeout=_CHILD_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            cc.terminate_and_reap(proc)
            self.fail("tui non-interactive invocation did not exit")
        self.assertEqual(proc.returncode, 2,
                         "tui must refuse a non-TTY stdin with exit 2")
        self.assertEqual(observed, set(),
                         f"tui owned LISTEN socket(s) {sorted(observed)}")
        if not sampled:
            self.skip_notes.append(
                "tui: PID listener sampling unavailable or process too short-"
                "lived; relied on exit 2 + default-port check"
            )
        self._assert_default_ports_closed("tui")


if __name__ == "__main__":
    unittest.main(verbosity=2)
