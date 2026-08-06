# QA Section 12 - CLI Link Get

## Category

CLI command mode and command-visible persistence.

## Purpose

Validate that a link created by the CLI can be read back by slug and id through
follow-up CLI commands in the same isolated working directory.

## E2E analog

- Executable script:
  `tests/e2e/scripts/sections/12_cli_link_get.sh`

## Preconditions

- The `url_shortener` binary is built.
- `URLSHORTENER_BIN` is set, or the binary exists in a standard build location
  checked by the script.
- The scenario runs in an isolated temporary directory so persisted state does
  not leak across test runs.

## Manual procedure

1. Create a link:

   ```bash
   url_shortener link create \
     --url https://e2e-get.example.com \
     --slug e2e-get-slug
   ```

2. Capture `id` from stdout JSON.
3. Read by slug:

   ```bash
   url_shortener link get --slug e2e-get-slug
   ```

4. Read by id:

   ```bash
   url_shortener link get --id <captured-id>
   ```

5. Compare all three JSON objects.

## Automated assertions

The e2e script validates:

- create output contains a non-empty `id`;
- get-by-slug output is valid JSON;
- get-by-id output is valid JSON;
- both get responses preserve the created `id`, `slug`, `url`, and `status`.

## Acceptance criteria

- Create exits with code `0`.
- Get by slug exits with code `0`.
- Get by id exits with code `0`.
- Console JSON from both get commands matches the created link data.
- State is scoped to the scenario working directory.
