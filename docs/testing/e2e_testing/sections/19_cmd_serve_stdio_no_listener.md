# QA Section 19 - `cmd` / `serve-stdio` / `tui` No Listener

## Category

CLI/server separation — unified local modes.

## Purpose

Prove that the three unified local entry points (`cmd`, `serve-stdio`, and
`tui`) never bind a listening socket and never create or write the legacy
`uri.txt` URI-store. This extends section 13 (`link` no-listener) to the
command-registry-based entry points (Milestone 0006 contract C5; Task 07.0
subtask 21).

## E2E analog

- Executable script:
  `tests/e2e/scripts/sections/19_cmd_serve_stdio_no_listener.sh`

## Preconditions

- The `url_shortener` binary is built.
- `URLSHORTENER_BIN` is set, or the binary exists in a standard build location
  checked by the script.
- Ports `8000` and `9999` are closed before the scenario starts. If another
  process already owns a tested port, the section skips the affected check to
  avoid false failures.
- `python3` is on PATH (required for the loopback connect probe and the
  `uri.txt` byte+mtime fingerprint).

## Manual procedure

### Check A1 — `cmd` exits with no listener

1. Verify port `8000` is closed.
2. Run:

   ```bash
   url_shortener cmd command.list
   ```

3. Wait for the command to exit.
4. Verify port `8000` is still closed.

### Check A2 — live `serve-stdio` owns no LISTEN socket

1. Start `serve-stdio` with its stdin kept open by a FIFO:

   ```bash
   mkfifo /tmp/stdio_in
   url_shortener serve-stdio < /tmp/stdio_in &
   stdio_pid=$!
   exec 9>/tmp/stdio_in
   printf '%s\n' '{"v":1,"command":"command.list"}' >&9
   sleep 0.3
   ```

2. While the process is alive, verify it owns no TCP or UNIX-domain LISTEN
   socket (use `lsof -nP -a -p $stdio_pid -iTCP -sTCP:LISTEN` on macOS or
   `ss -ltnp 2>/dev/null | grep "pid=$stdio_pid,"` on Linux, with a loopback
   port-8000 connect probe as the documented fallback).
3. Close stdin and reap the process: `exec 9>&-; wait $stdio_pid`.
4. Assert the check returned "no listener".

### Check A3 — non-interactive `tui` exits `2` with no listener

1. Run:

   ```bash
   url_shortener tui </dev/null
   ```

2. Wait for the command to exit.
3. Verify the exit code is `2` (non-TTY refusal).
4. Verify port `8000` is still closed.

### Check B — sentinel `uri.txt` unchanged

1. Create a sentinel file `uri.txt` with known content in a fresh working
   directory.
2. Run `cmd command.list` and `serve-stdio` (one NDJSON request) from that
   working directory.
3. Verify the sentinel is byte-identical (sha256 + mtime) after both runs.
4. Verify no additional `uri*.txt` files were created.

### Check C — `--port 9999` is rejected or ignored

1. Run:

   ```bash
   url_shortener cmd --port 9999 command.list
   ```

2. Wait for exit.
3. Verify port `9999` is still closed.

## Automated assertions

The e2e script uses a loopback connect probe (`python3`) for the port checks,
and `lsof` / `ss` where available (with the connect probe as fallback, and a
positive-control test to validate that `lsof` can actually enumerate sockets).
All waits are bounded with `run_with_timeout` from
`tests/e2e/scripts/common.sh`; the script returns `124` on timeout.

## Acceptance criteria

- `cmd` exits cleanly and leaves no listening socket on any port.
- A live `serve-stdio` process owns no TCP or UNIX-domain LISTEN socket.
- Non-TTY `tui` exits `2` and leaves no listening socket.
- The `uri.txt` sentinel is byte-identical before and after `cmd` and
  `serve-stdio` run in the same working directory.
- `--port 9999` does not cause `cmd` to open a listener on port `9999`.
- Failure indicates a local mode incorrectly started server mode or wrote the
  URI store.
