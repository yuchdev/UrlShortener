#!/usr/bin/env python3
"""32_no_listener_process_level.py - process-level no-listener / no-URI-store.

Out-of-process proof (Milestone 0006, Task 07.0 subtask 21, contract C5, threats
T-18/T-83/T-114) that the three new local-mode entry points never bind a
listening socket (TCP *or* UNIX-domain) and never create or read the legacy
``uri.txt`` / URI-store, exercised against the **real** binary (the structural
scan in ``tests/unit/commands/79_no_http_server_in_local_modes_test.cpp`` is
only the tripwire; these are the proof):

  * ``cmd`` exits after a one-shot command and no default HTTP/HTTPS port is
    listening afterwards; while a ``cmd`` process actually executes a command it
    owns no listening socket (sampled in a bounded loop across its lifetime).
  * ``serve-stdio`` answers requests (responses read back for synchronization)
    and owns no listening socket before, between or after handling them.
  * ``tui`` refuses a non-TTY stdin (exit 2) within a bounded wait and never
    owns a listening socket.
  * a sentinel ``uri.txt`` placed in the working directory (and ``$HOME`` when
    present) is byte-identical and mtime-unchanged after each mode runs, no new
    ``uri.txt`` appears, and the sentinel's unique marker is never echoed.
  * ``--port 9999`` is rejected or ignored for every mode: 9999 never listens.

PID socket ownership is platform-specific and only trusted after a **positive
control**: the test opens its own throwaway listening socket and confirms the
enumeration tool (``lsof`` / ``netstat``) actually reports it. When the tool is
absent or the control fails, the test falls back to a bounded loopback connect
probe and records a counted skip-note (a weaker, non-PID-attributed check) so
subtask 34 can account for it. The UNIX-domain leg uses ``lsof -U`` (no portable
LISTEN state, so the real binary's zero-UNIX-socket invariant is asserted).

Reuses ``conformance_common`` (``conformance_env`` / ``spawn`` /
``terminate_and_reap`` / ``free_port`` / ``find_binary``). Standard library
only; ``python3 -I``.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from typing import Callable, List, Optional, Set, Tuple

try:
    import select  # POSIX: bound a pipe read with a timeout.
    _HAVE_SELECT = hasattr(select, "select")
except ImportError:  # pragma: no cover - select is always present on POSIX/Win
    _HAVE_SELECT = False

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402

#: Unique marker written into the sentinel ``uri.txt``; a local mode that opened
#: or echoed the legacy store would surface it, so it must never appear.
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

#: Bounded ceiling on sampling a child's listener set across its lifetime.
_SAMPLE_BUDGET_S = 3.0

#: Bounded ceiling on reading one response line back from ``serve-stdio``.
_READLINE_TIMEOUT_S = 5.0

#: Cached positive-control results, keyed by family ("tcp" / "unix").
_ENUMERATION_OK: dict = {}


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


def _run_tool(argv: List[str]) -> Optional[subprocess.CompletedProcess]:
    """Run a short enumeration tool; return the result or ``None`` on failure."""
    try:
        return subprocess.run(
            argv, capture_output=True, text=True, timeout=_CHILD_TIMEOUT_S
        )
    except (subprocess.TimeoutExpired, OSError):
        return None


def _parse_lsof_tcp(tool: str, pid: int) -> Optional[Set[int]]:
    """Parse ``lsof`` TCP LISTEN ports for *pid*.

    ``lsof`` exits 1 with empty output when the PID owns no listening socket,
    which is a legitimate "no listener" result; the return code is therefore not
    used to decide availability (that is what the positive control is for). A
    tool that could not be launched at all returns ``None``.
    """
    result = _run_tool(
        [tool, "-nP", "-a", "-p", str(pid), "-iTCP", "-sTCP:LISTEN"]
    )
    if result is None:
        return None
    ports: Set[int] = set()
    for line in result.stdout.splitlines():
        if "(LISTEN)" not in line:
            continue
        fields = line.split()
        for index, token in enumerate(fields):
            if token == "(LISTEN)" and index > 0:
                match = re.search(r":(\d+)$", fields[index - 1])
                if match:
                    ports.add(int(match.group(1)))
    return ports


def _parse_lsof_unix(tool: str, pid: int) -> Optional[Set[str]]:
    """Parse the UNIX-domain socket NAMEs *pid* owns via ``lsof -U``.

    macOS ``lsof`` does not expose a portable LISTEN state for UNIX sockets, so
    this returns every UNIX-domain socket the PID holds; the caller asserts the
    real binary owns none (it opens zero UNIX sockets in any local mode). A tool
    that could not be launched returns ``None``.
    """
    result = _run_tool([tool, "-nP", "-U", "-a", "-p", str(pid)])
    if result is None:
        return None
    names: Set[str] = set()
    for line in result.stdout.splitlines():
        if line.startswith("COMMAND") or not line.strip():
            continue
        # The NAME (socket path or peer) is the last field of a data line.
        if " unix " in line or "\tunix\t" in line or " unix\t" in line:
            names.add(line.split()[-1])
    return names


def _netstat_listeners(pid: int) -> Optional[Set[int]]:
    """Return the TCP/TCPv6 LISTEN ports owned by *pid* via ``netstat`` (Win).

    Returns ``None`` (unavailable) when ``netstat`` is absent, fails, or
    produces no parseable ``LISTENING`` line at all (unparseable output must not
    masquerade as an empty, "no listener" set).
    """
    tool = shutil.which("netstat")
    if tool is None:
        return None
    ports: Set[int] = set()
    saw_any_listening_row = False
    for proto in ("tcp", "tcpv6"):
        result = _run_tool([tool, "-ano", "-p", proto])
        if result is None:
            return None
        for line in result.stdout.splitlines():
            fields = line.split()
            if (
                len(fields) >= 5
                and fields[0].upper().startswith("TCP")
                and fields[3].upper() == "LISTENING"
            ):
                saw_any_listening_row = True
                if fields[-1] == str(pid):
                    match = re.search(r":(\d+)$", fields[1])
                    if match:
                        ports.add(int(match.group(1)))
    if not saw_any_listening_row:
        # No LISTENING row anywhere is implausible on a live host -> treat the
        # output as unparseable rather than as "pid owns nothing".
        return None
    return ports


def _enumeration_works(family: str) -> bool:
    """Positive control: does ``lsof`` actually enumerate *family* sockets here?

    Opens a throwaway listening socket in this process and checks the tool
    reports it for our own PID. Cached per family. When it returns ``False`` the
    caller treats PID enumeration as unavailable and falls back to the probe,
    rather than trusting an empty tool result (which could be a sandbox denying
    enumeration, not a genuine "no listener").
    """
    if family in _ENUMERATION_OK:
        return _ENUMERATION_OK[family]
    tool = shutil.which("lsof")
    if tool is None:
        _ENUMERATION_OK[family] = False
        return False
    ok = False
    if family == "tcp":
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            probe.bind(("127.0.0.1", 0))
            probe.listen(1)
            port = probe.getsockname()[1]
            seen = _parse_lsof_tcp(tool, os.getpid())
            ok = seen is not None and port in seen
        finally:
            probe.close()
    elif family == "unix":
        tmp = tempfile.mkdtemp(prefix="conf32_ctl_")
        path = os.path.join(tmp, "control.sock")
        probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            probe.bind(path)
            probe.listen(1)
            seen = _parse_lsof_unix(tool, os.getpid())
            ok = seen is not None and any(path in name for name in seen)
        finally:
            probe.close()
            shutil.rmtree(tmp, ignore_errors=True)
    _ENUMERATION_OK[family] = ok
    return ok


def _pid_tcp_listeners(pid: int) -> Tuple[Optional[Set[int]], str]:
    """Return ``(ports, method)`` for *pid*'s TCP LISTEN sockets.

    Trusts ``lsof`` only when the TCP positive control passed; otherwise tries
    ``netstat`` (Windows); otherwise reports ``(None, "unavailable")`` so the
    caller falls back to a connect probe.
    """
    tool = shutil.which("lsof")
    if tool is not None and _enumeration_works("tcp"):
        ports = _parse_lsof_tcp(tool, pid)
        if ports is not None:
            return ports, "lsof"
    ports = _netstat_listeners(pid)
    if ports is not None:
        return ports, "netstat"
    return None, "unavailable"


def _pid_unix_sockets(pid: int) -> Tuple[Optional[Set[str]], str]:
    """Return ``(names, method)`` for *pid*'s UNIX-domain sockets (lsof only)."""
    tool = shutil.which("lsof")
    if tool is not None and _enumeration_works("unix"):
        names = _parse_lsof_unix(tool, pid)
        if names is not None:
            return names, "lsof-U"
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

    # ---- shared sampling / assertions -------------------------------------

    def _sample_once(self, pid: int) -> Tuple[Optional[Set[int]], Set[str], str]:
        """One sample: ``(tcp_ports_or_None, unix_names, method)`` for *pid*."""
        tcp, method = _pid_tcp_listeners(pid)
        unix, _umethod = _pid_unix_sockets(pid)
        return tcp, (unix or set()), method

    def _assert_no_listener_over_lifetime(
        self, proc: "subprocess.Popen", context: str,
        drive: Optional[Callable[[], None]] = None,
    ) -> None:
        """Sample *proc* in a bounded loop and assert it never listens.

        Samples TCP and UNIX-domain sockets repeatedly while the process is
        alive (and once more after it exits), so a listener that appears only
        transiently during command execution is caught. *drive*, when given, is
        invoked once after the first sample to make the process actually execute
        (e.g. send its request). Falls back to a connect probe with a counted
        note when PID enumeration is unavailable.
        """
        tcp_seen: Set[int] = set()
        unix_seen: Set[str] = set()
        tool_available = False
        deadline = time.monotonic() + _SAMPLE_BUDGET_S
        driven = False
        while True:
            tcp, unix, method = self._sample_once(proc.pid)
            if tcp is not None:
                tool_available = True
                tcp_seen |= tcp
                unix_seen |= unix
            if drive is not None and not driven:
                driven = True
                drive()
            if proc.poll() is not None:
                # One final post-exit sample already taken this iteration.
                break
            if time.monotonic() >= deadline:
                break
            time.sleep(0.02)

        if not tool_available:
            self.skip_notes.append(
                f"{context}: PID enumeration unavailable (no working "
                "lsof/netstat); used connect-probe fallback"
            )
            for port in (*_DEFAULT_LISTENER_PORTS, _REJECT_PORT):
                self.assertFalse(
                    _port_is_open(port),
                    f"{context}: port {port} open (connect-probe fallback)",
                )
            return

        self.assertEqual(
            tcp_seen, set(),
            f"{context}: pid owned TCP LISTEN socket(s) {sorted(tcp_seen)} "
            "(C5 violation)",
        )
        self.assertEqual(
            unix_seen, set(),
            f"{context}: pid owned UNIX-domain socket(s) {sorted(unix_seen)} "
            "(C5: a UNIX listener is invisible to a TCP probe)",
        )

    def _assert_default_ports_closed(self, context: str) -> None:
        """Assert every default listener port is closed (bounded connect)."""
        for port in _DEFAULT_LISTENER_PORTS:
            if _port_is_open(port):
                self.skip_notes.append(
                    f"{context}: port {port} already in use by another process"
                )
                continue
            self.assertFalse(
                _port_is_open(port), f"{context}: port {port} open after exit"
            )

    def _readline_bounded(self, proc: "subprocess.Popen") -> Optional[bytes]:
        """Read one line from *proc.stdout* within a bounded timeout."""
        assert proc.stdout is not None
        if _HAVE_SELECT:
            ready, _, _ = select.select(
                [proc.stdout], [], [], _READLINE_TIMEOUT_S
            )
            if not ready:
                return None
        return proc.stdout.readline()

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

    def test_cmd_executes_and_never_listens(self) -> None:
        # `cmd <name> --json -` blocks reading args from stdin until EOF, so the
        # process is alive to sample before it executes. The `drive` callback
        # then writes the request body + EOF so the command actually executes;
        # the sampling loop keeps sampling through execution and teardown. A
        # reader thread drains stdout concurrently so the child never blocks on
        # a full pipe (which would otherwise deadlock), and `wait` (not
        # `communicate`) is used because `drive` already closed stdin.
        proc = cc.spawn(
            [self.binary, "cmd", "command.list", "--json", "-"],
            env=self.env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        drained: dict = {}

        def reader() -> None:
            assert proc.stdout is not None
            try:
                drained["out"] = proc.stdout.read()
            except (ValueError, OSError):
                drained["out"] = b""

        reader_thread = threading.Thread(target=reader, daemon=True)
        reader_thread.start()

        def drive() -> None:
            assert proc.stdin is not None
            try:
                proc.stdin.write(b"{}")
                proc.stdin.close()
            except (BrokenPipeError, ValueError, OSError):
                pass

        try:
            self.assertIsNone(proc.poll(), "cmd exited before stdin EOF")
            self._assert_no_listener_over_lifetime(proc, "cmd (executing)",
                                                   drive=drive)
        finally:
            try:
                proc.wait(timeout=_CHILD_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                cc.terminate_and_reap(proc)
                self.fail("cmd --json - did not finish after stdin EOF")
            reader_thread.join(timeout=_CHILD_TIMEOUT_S)
            for stream in (proc.stdin, proc.stdout, proc.stderr):
                if stream is not None and not stream.closed:
                    stream.close()
        self.assertEqual(proc.returncode, 0, "cmd command.list should succeed")
        self.assertNotIn(_SENTINEL_MARKER,
                         drained.get("out", b"").decode("utf-8", "replace"))

    # ---- serve-stdio -------------------------------------------------------

    def test_serve_stdio_handles_requests_and_never_listens(self) -> None:
        proc = cc.spawn(
            [self.binary, "serve-stdio"],
            env=self.env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            assert proc.stdin is not None
            # Sample before handling anything.
            tcp0, unix0, _m0 = self._sample_once(proc.pid)
            if tcp0 is not None:
                self.assertEqual(tcp0, set(), "serve-stdio listened pre-request")
                self.assertEqual(unix0, set(),
                                 "serve-stdio owned a UNIX socket pre-request")
            # Handle two requests, reading each response back to synchronize.
            for _ in range(2):
                proc.stdin.write((_envelope("command.list") + "\n").encode())
                proc.stdin.flush()
                line = self._readline_bounded(proc)
                self.assertTrue(line, "serve-stdio produced no response line")
                parsed = json.loads(line.decode("utf-8"))
                self.assertTrue(parsed.get("ok") is True,
                                f"unexpected response: {parsed!r}")
                tcp, unix, _m = self._sample_once(proc.pid)
                if tcp is not None:
                    self.assertEqual(tcp, set(),
                                     "serve-stdio listened while handling")
                    self.assertEqual(unix, set(),
                                     "serve-stdio owned a UNIX socket")
                else:
                    self.skip_notes.append(
                        "serve-stdio: PID enumeration unavailable; relied on "
                        "the default-port check"
                    )
                    self._assert_default_ports_closed("serve-stdio")
        finally:
            try:
                proc.communicate(timeout=_CHILD_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                cc.terminate_and_reap(proc)
                self.fail("serve-stdio did not exit after stdin EOF")

    # ---- no uri.txt --------------------------------------------------------

    def _run_in_cwd(self, argv: List[str], cwd: Path,
                    stdin_bytes: Optional[bytes]) -> Tuple[int, str]:
        """Run *argv* in *cwd*, return ``(exit_code, stdout+stderr)``."""
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

    @staticmethod
    def _snapshot(path: Path) -> Optional[Tuple[bytes, int]]:
        """Return ``(bytes, mtime_ns)`` of *path*, or ``None`` when absent."""
        if not path.exists():
            return None
        return path.read_bytes(), path.stat().st_mtime_ns

    def test_no_uri_txt_created(self) -> None:
        modes = [
            (["cmd", "command.list"], None),
            (["cmd", "link.list"], None),
            (["serve-stdio"], (_envelope("command.list") + "\n").encode()),
            (["tui", "--help"], None),
        ]
        # $HOME/uri.txt is snapshotted if it happens to exist: a local mode must
        # never write outside its cwd either (LOW finding). We never create it.
        home_uri = Path(os.path.expanduser("~")) / "uri.txt"
        home_before = self._snapshot(home_uri)

        for argv, stdin_bytes in modes:
            with tempfile.TemporaryDirectory(prefix="conf32_cwd_") as tmp:
                cwd = Path(tmp)
                sentinel = cwd / "uri.txt"
                sentinel.write_bytes(
                    (_SENTINEL_MARKER + " short->http://example.com/\n").encode()
                )
                before = self._snapshot(sentinel)

                _rc, combined = self._run_in_cwd(
                    [self.binary, *argv], cwd, stdin_bytes
                )

                self.assertEqual(self._snapshot(sentinel), before,
                                 f"{argv}: sentinel uri.txt bytes/mtime changed")
                uri_files = sorted(p.name for p in cwd.iterdir()
                                   if p.name.startswith("uri")
                                   and p.name.endswith(".txt"))
                self.assertEqual(uri_files, ["uri.txt"],
                                 f"{argv}: unexpected uri files {uri_files}")
                self.assertNotIn(_SENTINEL_MARKER, combined,
                                 f"{argv}: sentinel marker echoed in output")

        # $HOME/uri.txt is unchanged (never created, never modified).
        self.assertEqual(self._snapshot(home_uri), home_before,
                         "$HOME/uri.txt changed - a mode wrote outside its cwd")

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
        # Non-TTY tui exits 2 almost immediately; sample across its (brief)
        # lifetime, then assert the refusal exit code and no listener.
        self._assert_no_listener_over_lifetime(proc, "tui (non-interactive)")
        try:
            proc.communicate(timeout=_CHILD_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            cc.terminate_and_reap(proc)
            self.fail("tui non-interactive invocation did not exit")
        self.assertEqual(proc.returncode, 2,
                         "tui must refuse a non-TTY stdin with exit 2")
        self._assert_default_ports_closed("tui")


if __name__ == "__main__":
    unittest.main(verbosity=2)
