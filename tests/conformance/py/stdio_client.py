#!/usr/bin/env python3
"""stdio_client.py - a timeout-bounded client for the ``serve-stdio`` transport.

Shared by the two Task 07.0 subtask 07 deliverables - the corpus adapter
(``11_stdio_corpus.py``) and the framing tests (``12_stdio_framing.py``) - so
neither duplicates the reader-thread / queue machinery and both obey the one
process-spawn contract ruled at the 07.0/05 review:

  * the child is started with :func:`conformance_common.spawn` (an explicit argv
    list, never a shell) and stopped with
    :func:`conformance_common.terminate_and_reap` (terminate -> wait -> kill ->
    wait), so an orphaned binary can never survive a failure;
  * every read is served from a background reader thread through a
    :class:`queue.Queue`, so a hung or crashed child fails the caller by
    *timeout* instead of blocking CTest forever - there is no blocking read
    without a deadline.

This mirrors the reader-thread pattern of the process-level
``tests/integration/cli/stdio_session.py`` (05.0), re-expressed on top of the
conformance ``spawn`` / ``terminate_and_reap`` helpers that the corpus shims are
required to use. All I/O is bytes, so Windows never rewrites ``\\n`` and non-ASCII
payloads round-trip verbatim.

The module is Python standard library only and runs under ``python3 -I``: the
sibling ``conformance_common`` import is resolved from ``__file__``.
"""

from __future__ import annotations

import json
import queue
import subprocess
import threading
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

import sys

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402


class StdioProc:
    """A single live ``serve-stdio`` child process with framed, bounded I/O.

    The child is spawned via :func:`conformance_common.spawn` (byte pipes on all
    three streams) and owned by this object; always release it with :meth:`stop`
    (the adapter's ``end_case`` and the framing tests' ``addCleanup`` both do).

    :ivar _proc: The owned ``subprocess.Popen`` started through ``cc.spawn``.
    """

    def __init__(
        self,
        binary: str,
        *args: str,
        cwd: Optional[str] = None,
        env: Optional[Dict[str, str]] = None,
    ) -> None:
        """Spawn ``[binary, *args]`` and start the stdout/stderr reader threads.

        :param binary: Absolute path to the ``url_shortener`` executable.
        :param args: Arguments after the binary, e.g. ``"serve-stdio"`` then any
            flags (``"--read-only"``, ``"--storage-config", <path>``).
        :param cwd: Working directory for the child (``None`` inherits).
        :param env: Full environment mapping for the child (``None`` inherits);
            pass a sanitised copy so a stray ``URL_SHORTENER_STORAGE_CONFIG`` can
            never override the case backend (threat model T-41).
        """
        self._proc = cc.spawn(
            [binary, *args],
            cwd=cwd,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._out_queue: "queue.Queue[Optional[bytes]]" = queue.Queue()
        self._err_chunks: List[bytes] = []
        self._out_thread = threading.Thread(target=self._pump_stdout, daemon=True)
        self._err_thread = threading.Thread(target=self._pump_stderr, daemon=True)
        self._out_thread.start()
        self._err_thread.start()

    # -- reader threads -----------------------------------------------------

    def _pump_stdout(self) -> None:
        """Push each stdout line onto the queue; push ``None`` at EOF.

        The ``None`` sentinel lets :meth:`read_line` and :meth:`expect_quiet`
        distinguish "no line yet" (a timeout) from "the stream closed" (EOF).
        """
        try:
            stream = self._proc.stdout
            assert stream is not None
            for raw in iter(stream.readline, b""):
                self._out_queue.put(raw)
        except (OSError, ValueError):
            pass
        finally:
            self._out_queue.put(None)

    def _pump_stderr(self) -> None:
        """Accumulate the child's stderr bytes until the stream closes."""
        try:
            stream = self._proc.stderr
            assert stream is not None
            for chunk in iter(lambda: stream.read(4096), b""):
                self._err_chunks.append(chunk)
        except (OSError, ValueError):
            pass

    # -- writing ------------------------------------------------------------

    def write_bytes(self, data: bytes) -> None:
        """Write *data* verbatim to stdin and flush (for exact framing control).

        :param data: The raw bytes to send, including the line terminator the
            test wants (``\\n``, ``\\r\\n`` or none).
        """
        stream = self._proc.stdin
        assert stream is not None
        stream.write(data)
        stream.flush()

    def write_line(self, text: str) -> None:
        """Write one UTF-8 text line terminated by a single ``\\n``.

        :param text: The line content; must not itself contain a newline (a
            multi-line payload is a framing test, written with
            :meth:`write_bytes`).
        """
        self.write_bytes((text + "\n").encode("utf-8"))

    def send_envelope(self, obj: Dict[str, Any]) -> None:
        """Serialise *obj* as a compact JSON envelope and send it as one line.

        :param obj: The request envelope dict (``ensure_ascii=False`` so non-ASCII
            survives the UTF-8 round trip).
        """
        self.write_line(json.dumps(obj, ensure_ascii=False))

    # -- reading ------------------------------------------------------------

    def read_line(self, timeout: float) -> str:
        """Return the next stdout line, decoded and stripped of its terminator.

        :param timeout: Seconds to wait for a line before failing.
        :return: The decoded line (trailing ``\\r`` / ``\\n`` removed).
        :raises TimeoutError: No line arrived within *timeout* (a hung child).
        :raises EOFError: stdout closed before another line arrived.
        """
        try:
            raw = self._out_queue.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError(
                "no serve-stdio line within %.1fs; stderr=%r"
                % (timeout, self.stderr_text())
            )
        if raw is None:
            raise EOFError(
                "serve-stdio stdout closed before a line; stderr=%r"
                % (self.stderr_text(),)
            )
        return raw.decode("utf-8").rstrip("\r\n")

    def read_json(self, timeout: float) -> Dict[str, Any]:
        """Return the next stdout line decoded as a JSON object.

        A stray non-JSON line (e.g. a log line that leaked onto stdout) raises
        ``json.JSONDecodeError`` here, which is exactly the stdout-purity
        assertion the framing tests rely on.

        :param timeout: Seconds to wait for the line.
        :return: The decoded response envelope.
        """
        return json.loads(self.read_line(timeout))

    def request(self, obj: Dict[str, Any], timeout: float = 10.0) -> Dict[str, Any]:
        """Send one envelope and return the next response envelope.

        :param obj: The request envelope dict.
        :param timeout: Seconds to wait for the response.
        :return: The decoded response envelope.
        """
        self.send_envelope(obj)
        return self.read_json(timeout)

    def expect_quiet(self, timeout: float) -> None:
        """Assert no stdout line arrives within *timeout*.

        An EOF sentinel also counts as "quiet" because the child produced no
        further envelope. Used for inputs the protocol swallows (blank lines) and
        to prove there is no trailing partial output after a clean shutdown.

        :param timeout: Quiet window to observe.
        :raises AssertionError: A line arrived unexpectedly.
        """
        try:
            raw = self._out_queue.get(timeout=timeout)
        except queue.Empty:
            return
        if raw is None:
            return
        raise AssertionError("unexpected serve-stdio output: %r" % (raw,))

    # -- lifecycle ----------------------------------------------------------

    def close_stdin(self) -> None:
        """Close the child's stdin, signalling EOF (the only graceful stop)."""
        try:
            if self._proc.stdin is not None:
                self._proc.stdin.close()
        except (BrokenPipeError, OSError):
            pass

    def wait(self, timeout: float = 15.0) -> int:
        """Wait for the child to exit and return its exit code.

        Joins the reader threads so :meth:`stderr_text` is complete afterwards.

        :param timeout: Seconds to wait before raising.
        :return: The process exit code.
        :raises subprocess.TimeoutExpired: The child did not exit in time.
        """
        rc = self._proc.wait(timeout=timeout)
        self._out_thread.join(timeout=2.0)
        self._err_thread.join(timeout=2.0)
        return rc

    @property
    def returncode(self) -> Optional[int]:
        """The child's exit code, or ``None`` while it is still running."""
        return self._proc.returncode

    @property
    def pid(self) -> int:
        """The child process id (used by the no-zombie teardown check)."""
        return self._proc.pid

    def stderr_text(self) -> str:
        """Return the stderr captured so far, decoded as UTF-8 (replacing)."""
        return b"".join(self._err_chunks).decode("utf-8", "replace")

    def stop(self) -> None:
        """Close stdin and reap the child via ``terminate_and_reap``.

        Idempotent and safe on an already-exited process, so it can run on every
        path (``end_case`` / ``addCleanup``). Only this object's own child is
        ever signalled. The reader threads are joined and the stdout/stderr pipes
        are closed so no file handle leaks past teardown.
        """
        self.close_stdin()
        cc.terminate_and_reap(self._proc, timeout=5.0)
        self._out_thread.join(timeout=2.0)
        self._err_thread.join(timeout=2.0)
        for stream in (self._proc.stdout, self._proc.stderr):
            try:
                if stream is not None:
                    stream.close()
            except (OSError, ValueError):
                pass


def _self_check() -> int:
    """Run a trivial ``command.list`` round trip against the real binary.

    Importable and runnable standalone (``python3 stdio_client.py`` with
    ``SIMPLE_HTTP_BIN`` set or a discoverable build): prints ``OK`` and exits 0.
    """
    binary = cc.find_binary()
    proc = StdioProc(binary, "serve-stdio")
    try:
        response = proc.request({"v": 1, "command": "command.list"})
        assert response["ok"] is True, response
        proc.close_stdin()
        assert proc.wait() == 0
    finally:
        proc.stop()
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(_self_check())
