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
import sys
import threading
from pathlib import Path
from typing import Any, Dict, List, Optional

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402

#: Hard cap on a single stdout line (CWE-400 guard). A child that streams bytes
#: with no newline can otherwise grow the reader's memory without bound. The cap
#: is the request body limit (``--max-request-body-bytes`` default 64 KiB) plus a
#: generous ceiling for the largest legitimate response envelope (e.g. a big
#: ``command.list`` or an inline export), well above anything the corpus emits.
_MAX_LINE_BYTES = 8 * 1024 * 1024

#: Bound on the number of unread stdout lines buffered in memory. Legitimate
#: pipelining (``test_ordered_pipeline`` sends 50) stays far under this; a child
#: that floods lines a stalled consumer never reads is capped here instead.
_STDOUT_QUEUE_MAX = 4096

#: Bound on retained stderr bytes; the head is kept and the tail is dropped with
#: a truncation note, so a chatty child cannot grow ``_err_chunks`` without end.
_MAX_STDERR_BYTES = 1 * 1024 * 1024

#: Deadline (seconds) for the send path: enqueuing onto the writer thread blocks
#: at most this long, so a child that stops draining stdin fails the caller by
#: timeout instead of blocking a pipelined burst forever. Scaled by
#: ``URLSHORTENER_TEST_TIMEOUT_SCALE`` for slow sanitizer builds.
_WRITE_TIMEOUT = cc.scale_timeout(30.0)

#: Deadline (seconds) the reader uses to hand a line to a stalled consumer before
#: declaring the bounded stdout queue overflowed. Scaled by
#: ``URLSHORTENER_TEST_TIMEOUT_SCALE`` for slow sanitizer builds.
_QUEUE_PUT_TIMEOUT = cc.scale_timeout(30.0)

#: Sentinel queued when a stdout line exceeds :data:`_MAX_LINE_BYTES`; surfaced by
#: :meth:`StdioProc.read_line` as a clear failure rather than a buffered blob.
_LINE_OVERFLOW = object()

#: Sentinel queued when the bounded stdout queue overflows (consumer not reading).
_QUEUE_OVERFLOW = object()


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
        self._out_queue: "queue.Queue[Any]" = queue.Queue(maxsize=_STDOUT_QUEUE_MAX)
        self._in_queue: "queue.Queue[Optional[bytes]]" = queue.Queue(
            maxsize=_STDOUT_QUEUE_MAX)
        self._err_chunks: List[bytes] = []
        self._err_total = 0
        self._err_truncated = False
        self._write_error: Optional[BaseException] = None
        self._out_thread = threading.Thread(target=self._pump_stdout, daemon=True)
        self._err_thread = threading.Thread(target=self._pump_stderr, daemon=True)
        self._in_thread = threading.Thread(target=self._pump_stdin, daemon=True)
        self._out_thread.start()
        self._err_thread.start()
        self._in_thread.start()

    # -- reader threads -----------------------------------------------------

    def _pump_stdout(self) -> None:
        """Push each bounded stdout line onto the queue; push ``None`` at EOF.

        Lines are read with a byte cap (:data:`_MAX_LINE_BYTES`): a physical line
        longer than the cap is drained and discarded, and a
        :data:`_LINE_OVERFLOW` sentinel is queued so :meth:`read_line` reports it
        instead of buffering an unbounded blob (CWE-400). The queue itself is
        bounded, so a stalled consumer makes the bounded ``put`` time out and a
        :data:`_QUEUE_OVERFLOW` sentinel is queued. The terminal ``None`` sentinel
        lets :meth:`read_line` / :meth:`expect_quiet` tell "no line yet" (timeout)
        from "the stream closed" (EOF).
        """
        try:
            stream = self._proc.stdout
            assert stream is not None
            while True:
                raw = stream.readline(_MAX_LINE_BYTES + 1)
                if not raw:
                    break
                if len(raw) > _MAX_LINE_BYTES and not raw.endswith(b"\n"):
                    # Oversize line: discard the rest of this physical line, then
                    # report the overflow once rather than keep buffering it.
                    while True:
                        tail = stream.readline(_MAX_LINE_BYTES + 1)
                        if not tail or tail.endswith(b"\n"):
                            break
                    if not self._offer(_LINE_OVERFLOW):
                        break
                    continue
                if not self._offer(raw):
                    break
        except (OSError, ValueError):
            pass
        finally:
            try:
                self._out_queue.put(None, timeout=_QUEUE_PUT_TIMEOUT)
            except queue.Full:
                pass

    def _offer(self, item: Any) -> bool:
        """Hand *item* to the bounded stdout queue; mark overflow on a stall.

        :param item: A line (``bytes``) or an overflow sentinel.
        :return: ``True`` if queued, ``False`` if the consumer stalled long enough
            to overflow the bounded queue (an overflow sentinel is queued and the
            reader should stop).
        """
        try:
            self._out_queue.put(item, timeout=_QUEUE_PUT_TIMEOUT)
            return True
        except queue.Full:
            try:
                self._out_queue.put_nowait(_QUEUE_OVERFLOW)
            except queue.Full:
                pass
            return False

    def _pump_stderr(self) -> None:
        """Accumulate bounded stderr bytes until the stream closes.

        At most :data:`_MAX_STDERR_BYTES` are retained (the head); further bytes
        are drained but discarded and a truncation note is appended by
        :meth:`stderr_text`, so a chatty child cannot grow memory without bound.
        """
        try:
            stream = self._proc.stderr
            assert stream is not None
            for chunk in iter(lambda: stream.read(4096), b""):
                if self._err_total >= _MAX_STDERR_BYTES:
                    self._err_truncated = True
                    continue
                take = chunk[: _MAX_STDERR_BYTES - self._err_total]
                self._err_chunks.append(take)
                self._err_total += len(take)
                if len(take) < len(chunk):
                    self._err_truncated = True
        except (OSError, ValueError):
            pass

    def _pump_stdin(self) -> None:
        """Drain the writer queue, writing each payload to stdin and flushing.

        Running the blocking ``write`` / ``flush`` off the caller's thread is what
        lets :meth:`write_bytes` enforce a deadline: a child that stops reading
        stalls this thread, the bounded writer queue fills, and the next enqueue
        times out instead of blocking a pipelined burst forever.
        """
        stream = self._proc.stdin
        while True:
            data = self._in_queue.get()
            if data is None:
                return
            try:
                assert stream is not None
                stream.write(data)
                stream.flush()
            except (OSError, ValueError) as exc:
                self._write_error = exc
                return

    # -- writing ------------------------------------------------------------

    def write_bytes(self, data: bytes) -> None:
        """Enqueue *data* for the writer thread with a bounded deadline.

        The bytes are written verbatim (exact framing control). The send path is
        bounded: if the child has stopped draining stdin and the writer backlog is
        full, the enqueue times out rather than blocking indefinitely.

        :param data: The raw bytes to send, including the line terminator the test
            wants (``\\n``, ``\\r\\n`` or none).
        :raises TimeoutError: The writer backlog stayed full for
            :data:`_WRITE_TIMEOUT` seconds (child not draining stdin).
        :raises OSError: A prior write failed (e.g. the child closed its stdin).
        """
        if self._write_error is not None:
            raise self._write_error
        try:
            self._in_queue.put(data, timeout=_WRITE_TIMEOUT)
        except queue.Full:
            raise TimeoutError(
                "serve-stdio stdin backlog stayed full for %.1fs; the child is "
                "not draining stdin" % _WRITE_TIMEOUT
            )

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
        if raw is _LINE_OVERFLOW:
            raise RuntimeError(
                "serve-stdio emitted a line exceeding %d bytes without a "
                "terminator; dropped to bound memory (CWE-400)" % _MAX_LINE_BYTES
            )
        if raw is _QUEUE_OVERFLOW:
            raise RuntimeError(
                "serve-stdio produced more than %d unread lines; the consumer "
                "stalled and the bounded queue overflowed" % _STDOUT_QUEUE_MAX
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
        """Flush the writer backlog, stop the writer, then close stdin (EOF).

        The writer thread is asked to stop with a ``None`` sentinel and joined
        (bounded) so any already-queued payloads are flushed before the EOF; the
        stdin fd is then closed, which is the only graceful stop. Idempotent.
        """
        try:
            self._in_queue.put(None, timeout=_WRITE_TIMEOUT)
        except queue.Full:
            pass
        self._in_thread.join(timeout=_WRITE_TIMEOUT)
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
        """Return the retained stderr, decoded as UTF-8 (replacing).

        When the stream was truncated at :data:`_MAX_STDERR_BYTES`, a trailing
        marker is appended so a caller scanning the text sees that the tail was
        dropped rather than silently missing.
        """
        text = b"".join(self._err_chunks).decode("utf-8", "replace")
        if self._err_truncated:
            text += "\n...[stderr truncated at %d bytes]" % _MAX_STDERR_BYTES
        return text

    def stop(self) -> None:
        """Close stdin and reap the child via ``terminate_and_reap``.

        Idempotent and safe on an already-exited process, so it can run on every
        path (``end_case`` / ``addCleanup``). Only this object's own child is
        ever signalled. The writer and reader threads are joined and the
        stdout/stderr pipes are closed so no file handle leaks past teardown.
        """
        self.close_stdin()
        cc.terminate_and_reap(self._proc, timeout=5.0)
        self._in_thread.join(timeout=2.0)
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
