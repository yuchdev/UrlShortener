#!/usr/bin/env bash
# tests/e2e/scripts/sections/12_cli_link_get.sh
#
# E2E CLI section 12: `link get` is process-local.
#
# The CLI builds a fresh in-process, in-memory LinkCommandService on every
# invocation (see src/cli/link_command_dispatch.cpp::DispatchLinkCommand) and
# persists nothing between processes. So a link created by one `link create`
# process is NOT visible to a later, separate `link get` process - even in the
# same working directory. This section asserts that process-local contract,
# consistent with tests/integration/cli/03_link_create_then_get_persists_state.py.
#
# PREREQUISITES:
#   1. CLI subcommand dispatch in main.cpp: "link create" AND "link get" branches.
#   2. link get --slug <SLUG> and --id <ID> are parsed.
#   3. A missing link exits non-zero with "Link not found" on stderr.
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

echo "=== 12_cli_link_get: create then get is process-local ==="

# Step 1 - create (must succeed and return a non-empty id).
CREATE_OUT="$(
  cd "$TMPDIR_CLI" && "$BINARY" link create \
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
assert_not_found() {
  local label="$1"; shift
  local stderr_out exit_code
  set +e
  stderr_out="$(cd "$TMPDIR_CLI" && "$BINARY" "$@" 2>&1 1>/dev/null)"
  exit_code=$?
  set -e
  if [[ $exit_code -eq 0 ]]; then
    echo "FAIL: $label unexpectedly succeeded; separate CLI processes must not share state" >&2
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

echo "PASS: 12_cli_link_get"
