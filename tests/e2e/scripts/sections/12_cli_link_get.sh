#!/usr/bin/env bash
# tests/e2e/scripts/sections/12_cli_link_get.sh
#
# E2E CLI section 12: `link get` isolation (default) and persistence (SQLite).
#
# Two-sided contract (milestone 0006, task 02.0 subtask 13):
#
#   * Isolation half (no --storage-config): the CLI builds a fresh in-process,
#     in-memory LinkCommandService on every invocation
#     (src/cli/link_command_dispatch.cpp::DispatchLinkCommand) and persists
#     nothing between processes, so a link created by one `link create` process
#     is NOT visible to a later, separate `link get` process -- even in the same
#     working directory. These steps run under `env -u URL_SHORTENER_STORAGE_CONFIG`
#     so a stray env var cannot silently select a persistent backend (T-41).
#
#   * Persistence half (SQLite --storage-config): separate CLI processes share
#     one database file, so a link created by one process IS visible to later
#     `link get --slug` and `link get --id` processes.
#
# Consistent with tests/integration/cli/03_link_create_then_get_persists_state.py.
#
# PREREQUISITES:
#   1. CLI subcommand dispatch in main.cpp: "link create" AND "link get" branches.
#   2. link get --slug <SLUG> and --id <ID> are parsed.
#   3. A missing link exits non-zero with "Link not found" on stderr.
#   4. --storage-config selects the backend; SQLite persists across processes.
#
# Does NOT use the mock HTTP service or SQLite state assertions.
set -euo pipefail

BINARY="${URLSHORTENER_BIN:-}"
if [[ -z "$BINARY" ]]; then
  for candidate in \
      cmake-build/url_shortener \
      cmake-build/Debug/url_shortener \
      build/url_shortener; do
    if [[ -x "$candidate" ]]; then
      BINARY="$candidate"
      break
    fi
  done
fi

if [[ -z "$BINARY" || ! -x "$BINARY" ]]; then
  echo "ERROR: url_shortener binary not found. Set URLSHORTENER_BIN or build first." >&2
  exit 2
fi

TMPDIR_CLI="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_CLI"' EXIT

echo "=== 12_cli_link_get: default isolation + SQLite persistence ==="

# --------------------------------------------------------------------------
# Isolation half (default in-memory backend; storage-config env var scrubbed).
# --------------------------------------------------------------------------

# Step 1 - create (must succeed and return a non-empty id).
CREATE_OUT="$(
  cd "$TMPDIR_CLI" && env -u URL_SHORTENER_STORAGE_CONFIG "$BINARY" link create \
    --url https://e2e-get.example.com \
    --slug e2e-get-slug \
    2>/dev/null
)"
echo "create stdout: $CREATE_OUT"

LINK_ID="$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('id',''))" "$CREATE_OUT" 2>/dev/null)"
if [[ -z "$LINK_ID" ]]; then
  echo "FAIL: create response did not include a non-empty 'id'" >&2
  exit 1
fi

# Helper: assert a `link get` invocation in a *new* process fails as not-found.
# The storage-config env var is scrubbed so the default in-memory backend is used.
assert_not_found() {
  local label="$1"; shift
  local stderr_out exit_code
  set +e
  stderr_out="$(cd "$TMPDIR_CLI" && env -u URL_SHORTENER_STORAGE_CONFIG "$BINARY" "$@" 2>&1 1>/dev/null)"
  exit_code=$?
  set -e
  if [[ $exit_code -eq 0 ]]; then
    echo "FAIL: $label unexpectedly succeeded; separate in-memory CLI processes must not share state" >&2
    exit 1
  fi
  if ! grep -q "Link not found" <<<"$stderr_out"; then
    echo "FAIL: $label did not report 'Link not found' (stderr: $stderr_out)" >&2
    exit 1
  fi
  echo "$label: not found (exit $exit_code) as expected"
}

# Step 2 - get by slug in a new process: must NOT find the link.
assert_not_found "get-by-slug" link get --slug e2e-get-slug

# Step 3 - get by id in a new process: must NOT find the link.
assert_not_found "get-by-id" link get --id "$LINK_ID"

# --------------------------------------------------------------------------
# Persistence half (SQLite --storage-config shared across processes).
# --------------------------------------------------------------------------

# Step 4 - write a SQLite storage config with an absolute, forward-slash path.
DB_PATH="$TMPDIR_CLI/e2e.sqlite3"
STORAGE_YAML="$TMPDIR_CLI/storage.yaml"
{
  echo "metadata:"
  echo "  backend: sqlite"
  echo "  sqlite_path: $DB_PATH"
} >"$STORAGE_YAML"

SQL_CREATE_OUT="$(
  cd "$TMPDIR_CLI" && "$BINARY" link create \
    --url https://e2e-sqlite.example.com \
    --slug e2e-sqlite-slug \
    --storage-config "$STORAGE_YAML" \
    2>/dev/null
)"
echo "sqlite create stdout: $SQL_CREATE_OUT"

SQL_LINK_ID="$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('id',''))" "$SQL_CREATE_OUT" 2>/dev/null)"
if [[ -z "$SQL_LINK_ID" ]]; then
  echo "FAIL: sqlite create response did not include a non-empty 'id'" >&2
  exit 1
fi

# Helper: assert a `link get` invocation in a *new* process finds the link and
# its slug matches, against the shared SQLite database.
assert_found_slug() {
  local label="$1"; shift
  local stdout_out exit_code slug
  set +e
  stdout_out="$(cd "$TMPDIR_CLI" && "$BINARY" "$@" --storage-config "$STORAGE_YAML" 2>/dev/null)"
  exit_code=$?
  set -e
  if [[ $exit_code -ne 0 ]]; then
    echo "FAIL: $label exited $exit_code; SQLite state must persist across processes" >&2
    exit 1
  fi
  slug="$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('slug',''))" "$stdout_out" 2>/dev/null)"
  if [[ "$slug" != "e2e-sqlite-slug" ]]; then
    echo "FAIL: $label returned slug '$slug', expected 'e2e-sqlite-slug' (stdout: $stdout_out)" >&2
    exit 1
  fi
  echo "$label: found (exit $exit_code, slug $slug) as expected"
}

# Step 5 - get by slug in a new process: must find the persisted link.
assert_found_slug "sqlite-get-by-slug" link get --slug e2e-sqlite-slug

# Step 6 - get by id in a new process: must find the persisted link.
assert_found_slug "sqlite-get-by-id" link get --id "$SQL_LINK_ID"

echo "PASS: 12_cli_link_get"
