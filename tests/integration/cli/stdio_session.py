"""
stdio_session.py - a reusable client for the `serve-stdio` NDJSON transport.

`StdioSession` spawns the real ``url_shortener`` binary in ``serve-stdio`` mode,
writes request envelopes to its stdin one NDJSON line at a time and reads the
response envelopes back from its stdout. It is deliberately independent of
``unittest`` so the Milestone 0007 (07.0/07) cross-transport conformance shim
can import and drive it without modification (subtask 18 success criterion 4).

Design notes (05.0/18 spec, "Classes and functions"):
  * All I/O is bytes: stdin/stdout/stderr are opened as binary pipes so Windows
    never rewrites ``\\n`` to ``\\r\\n`` and non-ASCII payloads survive a
    verbatim UTF-8 round trip.
  * ``recv`` is served by a background reader thread feeding a queue, so a hung
    or crashed child fails the caller by *timeout* instead of blocking CTest
    forever (a bug must fail fast, spec "Constraints").
  * stderr is drained by its own thread into a buffer; ``stderr_text`` returns
    the accumulated text for the no-request-data-in-stderr assertion (C6) and
    for diagnostics on timeout/EOF.

The class makes no assertions of its own; callers own the test logic.
"""
from __future__ import annotations

import json
import queue
import subprocess
import threading
import time
from typing import Any, Dict, List, Optional, Union


class StdioSession:
    """A single live ``serve-stdio`` child process and its framed streams.

    The constructor spawns ``[binary, *args]`` (callers pass ``"serve-stdio"``
    as the first element of *args*, followed by any mode flags such as
    ``--read-only`` or ``--path-root``). The child is owned by this object: use
    it as a context manager, or call :meth:`close` / :meth:`wait`, so the
    process and its pipes are always reclaimed.
    """

    def __init__(
        self,
        binary: str,
        *args: str,
        cwd: Optional[str] = None,
        env: Optional[Dict[str, str]] = None,
    ) -> None:
        """Spawn the binary with binary (byte) pipes and start the reader
        threads.

        :param binary: Absolute path to the ``url_shortener`` executable.
        :param args: Arguments appended after the binary, e.g.
            ``"serve-stdio", "--read-only"``.
        :param cwd: Working directory for the child (``None`` inherits).
        :param env: Full environment mapping for the child (``None`` inherits).
            Pass a sanitised copy to keep a stray
            ``URL_SHORTENER_STORAGE_CONFIG`` from leaking a persistent backend
            into an otherwise in-memory test (threat model T-41).
        """
        self._proc = subprocess.Popen(
            [binary, *args],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=cwd,
            env=env,
            bufsize=0,
        )
        self._out_queue: "queue.Queue[Optional[bytes]]" = queue.Queue()
        self._err_chunks: List[bytes] = []
        self._out_thread = threading.Thread(
            target=self._pump_stdout, daemon=True)
        self._err_thread = threading.Thread(
            target=self._pump_stderr, daemon=True)
        self._out_thread.start()
        self._err_thread.start()

    # -- reader threads -----------------------------------------------------

    def _pump_stdout(self) -> None:
        """Read stdout line by line into the queue; push ``None`` at EOF.

        The ``None`` sentinel lets :meth:`recv` distinguish "no response yet"
        (queue empty, a timeout) from "the stream closed" (an EOF the tests
        treat as a terminal condition).
        """
        try:
            stream = self._proc.stdout
            assert stream is not None
            while True:
                raw = stream.readline()
                if not raw:
                    break
                self._out_queue.put(raw)
        except (ValueError, OSError):
            # The pipe was closed underneath the blocking read (e.g. a test
            # deliberately closes stdout): treat as a clean end of stream.
            pass
        finally:
            self._out_queue.put(None)

    def _pump_stderr(self) -> None:
        """Accumulate the child's stderr bytes until the stream closes."""
        try:
            stream = self._proc.stderr
            assert stream is not None
            while True:
                chunk = stream.read(4096)
                if not chunk:
                    break
                self._err_chunks.append(chunk)
        except (ValueError, OSError):
            pass

    # -- writing ------------------------------------------------------------

    def send(self, obj_or_line: Union[Dict[str, Any], str, bytes]) -> None:
        """Write one request to the child's stdin and flush.

        :param obj_or_line: A ``dict`` (serialised with ``json.dumps`` and a
            trailing ``\\n``, UTF-8, non-ASCII preserved), a ``str`` (encoded
            UTF-8, a ``\\n`` appended when absent), or ``bytes`` (written
            verbatim so a test can control the exact framing, e.g. a ``\\r\\n``
            terminator or a deliberately malformed line).
        """
        if isinstance(obj_or_line, (bytes, bytearray)):
            data = bytes(obj_or_line)
        elif isinstance(obj_or_line, str):
            data = obj_or_line.encode("utf-8")
            if not data.endswith(b"\n"):
                data += b"\n"
        else:
            data = (json.dumps(obj_or_line, ensure_ascii=False) + "\n").encode(
                "utf-8")
        stream = self._proc.stdin
        assert stream is not None
        stream.write(data)
        stream.flush()

    def command(
        self,
        name: str,
        args: Optional[Dict[str, Any]] = None,
        **options: Any,
    ) -> Dict[str, Any]:
        """Send one command envelope and return the decoded response.

        :param name: Dotted command name (the envelope ``command`` field).
        :param args: Command arguments object, omitted when ``None``.
        :param options: Extra top-level envelope fields merged verbatim, e.g.
            ``id="c-1"`` or ``options={"dry_run": True}``. A ``timeout`` key is
            consumed as the :meth:`recv` deadline rather than placed on the
            wire.
        :return: The decoded response envelope.
        :raises TimeoutError: No response arrived within the deadline.
        :raises EOFError: The child closed stdout without responding.
        """
        timeout = options.pop("timeout", 5.0)
        req: Dict[str, Any] = {"v": 1, "command": name}
        if args is not None:
            req["args"] = args
        req.update(options)
        self.send(req)
        return self.recv(timeout=timeout)

    # -- reading ------------------------------------------------------------

    def recv(self, timeout: float = 5.0) -> Dict[str, Any]:
        """Return the next response envelope, decoded from one stdout line.

        :param timeout: Seconds to wait for a line before failing.
        :return: The decoded JSON object.
        :raises TimeoutError: No line within *timeout* (a hung/stalled child).
        :raises EOFError: stdout closed before another line arrived.
        """
        try:
            raw = self._out_queue.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError(
                "no serve-stdio response within %.1fs; stderr=%r"
                % (timeout, self.stderr_text()))
        if raw is None:
            raise EOFError(
                "serve-stdio stdout closed before a response; stderr=%r"
                % (self.stderr_text(),))
        return json.loads(raw.decode("utf-8").rstrip("\r\n"))

    def expect_no_response(self, timeout: float = 0.5) -> None:
        """Assert no response line arrives within *timeout*.

        Used for inputs the protocol swallows silently (blank/whitespace-only
        lines). An EOF sentinel also counts as "no response" because the child
        produced no envelope.

        :param timeout: Quiet window to observe.
        :raises AssertionError: A response line arrived unexpectedly.
        """
        try:
            raw = self._out_queue.get(timeout=timeout)
        except queue.Empty:
            return
        if raw is None:
            return
        raise AssertionError(
            "unexpected serve-stdio output: %r" % (raw.rstrip(b"\r\n"),))

    def recv_all(self, timeout: float = 10.0) -> List[Dict[str, Any]]:
        """Drain every remaining response line until stdout closes (EOF).

        :param timeout: Overall wall-clock budget for the drain.
        :return: The decoded envelopes in arrival order.
        :raises TimeoutError: stdout did not close within *timeout*.
        """
        out: List[Dict[str, Any]] = []
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("serve-stdio stdout did not close in time")
            try:
                raw = self._out_queue.get(timeout=remaining)
            except queue.Empty:
                raise TimeoutError("serve-stdio stdout did not close in time")
            if raw is None:
                return out
            out.append(json.loads(raw.decode("utf-8").rstrip("\r\n")))

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
        :return: The process exit code (negative means killed by a signal on
            POSIX; the tests assert this never happens).
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
        """The child process id."""
        return self._proc.pid

    def stderr_text(self) -> str:
        """Return the stderr captured so far, decoded as UTF-8 (replacing)."""
        return b"".join(self._err_chunks).decode("utf-8", "replace")

    def close(self) -> None:
        """Close stdin and ensure the child is reaped and pipes are released.

        Idempotent. If the child does not exit promptly after EOF it is killed
        (its own child only - never another process), so a stuck test cleans up
        rather than leaking a process.
        """
        self.close_stdin()
        try:
            self._proc.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            try:
                self._proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                pass
        for stream in (self._proc.stdout, self._proc.stderr):
            try:
                if stream is not None:
                    stream.close()
            except (OSError, ValueError):
                pass

    def __enter__(self) -> "StdioSession":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()


def _self_check() -> int:
    """Run a trivial ``command.list`` round trip (spec implementation step 1).

    Importable and runnable standalone: ``python stdio_session.py`` with
    ``SIMPLE_HTTP_BIN`` set (or a built binary discoverable by
    ``cli_integration_common.find_binary``). Prints ``OK`` and exits 0 on a
    well-formed response.
    """
    import os
    import sys

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from cli_integration_common import find_binary  # noqa: E402

    binary = find_binary()
    with StdioSession(binary, "serve-stdio") as session:
        response = session.command("command.list")
        assert response["v"] == 1, response
        assert response["ok"] is True, response
        names = {c["name"] for c in response["result"]["commands"]}
        assert {"link.create", "job.get", "link.import"} <= names, names
        session.close_stdin()
        rc = session.wait()
        assert rc == 0, "EOF should exit 0, got %r" % rc
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(_self_check())
