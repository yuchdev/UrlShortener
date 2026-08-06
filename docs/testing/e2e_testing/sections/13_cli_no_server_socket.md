# QA Section 13 - CLI Does Not Start Server

## Category

CLI/server separation.

## Purpose

Prove that one-shot CLI command mode does not start `HttpServer`, does not run
the service event loop, and does not leave the default HTTP port open.

## E2E analog

- Executable script:
  `tests/e2e/scripts/sections/13_cli_no_server_socket.sh`

## Preconditions

- The `url_shortener` binary is built.
- `URLSHORTENER_BIN` is set, or the binary exists in a standard build location
  checked by the script.
- Port `8000` is closed before the scenario starts. If another process already
  owns the port, the e2e script skips to avoid a false failure.

## Manual procedure

1. Verify port `8000` is closed.
2. Run:

   ```bash
   url_shortener link create \
     --url https://no-server.example.com \
     --base-domain http://sho.rt
   ```

3. Wait for the command to exit.
4. Verify port `8000` is still closed.

## Automated assertions

The e2e script uses Python socket probing before and after the CLI command.
The post-command check fails if `127.0.0.1:8000` accepts connections.

## Acceptance criteria

- CLI command exits instead of blocking as a service.
- Port `8000` remains closed after command completion.
- Failure indicates command mode incorrectly started server mode.
