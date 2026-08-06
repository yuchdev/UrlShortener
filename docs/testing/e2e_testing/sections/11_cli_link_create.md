# QA Section 11 - CLI Link Create

## Category

CLI command mode.

## Purpose

Validate that `url_shortener link create` executes as a one-shot command,
returns structured console output, and does not require the REST API server.

## E2E analog

- Executable script:
  `tests/e2e/scripts/sections/11_cli_link_create.sh`

## Preconditions

- The `url_shortener` binary is built.
- `URLSHORTENER_BIN` is set, or the binary exists in a standard build location
  checked by the script.
- No external service is required.

## Manual procedure

1. Run:

   ```bash
   url_shortener link create \
     --url https://e2e-create.example.com \
     --slug e2e-create-slug \
     --base-domain http://sho.rt
   ```

2. Confirm the process exits with code `0`.
3. Confirm stdout is a single JSON object.
4. Confirm the JSON contains:
   - `id` as a non-empty string;
   - `slug` equal to `e2e-create-slug`;
   - `url` equal to `https://e2e-create.example.com`;
   - `short_url` beginning with `http://sho.rt/`;
   - `status` equal to `active`.
5. Confirm no REST API server interaction is required.

## Automated assertions

The e2e script parses stdout with Python JSON tooling and fails if any required
field is missing or incorrect.

## Acceptance criteria

- Command exits successfully.
- Console output is machine-readable JSON.
- Created-link fields match the command input and configured base domain.
- No mock HTTP service or REST server is started by the scenario.
