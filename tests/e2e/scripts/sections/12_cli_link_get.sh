#!/usr/bin/env bash
# tests/e2e/scripts/sections/12_cli_link_get.sh
#
# E2E CLI section 12: link create then link get in the same working directory
# returns the persisted link data.
#
# PREREQUISITES:
#   1. CLI subcommand dispatch in main.cpp: "link create" AND "link get" branches.
#   2. main.cpp CLI: saves uri.txt after create, loads uri.txt before get.
#   3. link get --slug <SLUG> and --id <ID> are parsed.
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

echo "=== 12_cli_link_get: create then get ==="

# Step 1 – create
CREATE_OUT="$(
  "$BINARY" link create \
    --url https://e2e-get.example.com \
    --slug e2e-get-slug \
    2>/dev/null
)"
echo "create stdout: $CREATE_OUT"

# Extract id using Python (portable; avoids jq dependency).
LINK_ID="$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('id',''))" "$CREATE_OUT" 2>/dev/null)"

if [[ -z "$LINK_ID" ]]; then
  echo "FAIL: create response did not include a non-empty 'id'" >&2
  exit 1
fi

# Step 2 – get by slug
GET_SLUG_OUT="$(
  cd "$TMPDIR_CLI" && "$BINARY" link get --slug e2e-get-slug 2>/dev/null
)"
echo "get-by-slug stdout: $GET_SLUG_OUT"

# Step 3 – get by id
GET_ID_OUT="$(
  cd "$TMPDIR_CLI" && "$BINARY" link get --id "$LINK_ID" 2>/dev/null
)"
echo "get-by-id stdout: $GET_ID_OUT"

# Validate both responses.
python3 - "$CREATE_OUT" "$GET_SLUG_OUT" "$GET_ID_OUT" <<'PY'
import json, sys

create_raw, slug_raw, id_raw = sys.argv[1], sys.argv[2], sys.argv[3]

def load(raw, label):
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        print(f"FAIL: {label} is not valid JSON: {exc}", file=sys.stderr)
        print(f"raw: {raw!r}", file=sys.stderr)
        sys.exit(1)

create_data  = load(create_raw,  "create")
slug_data    = load(slug_raw,    "get-by-slug")
id_data      = load(id_raw,      "get-by-id")

failures = []

for label, data in [("get-by-slug", slug_data), ("get-by-id", id_data)]:
    if data.get("url") != "https://e2e-get.example.com":
        failures.append(f"{label}.url mismatch: {data.get('url')!r}")
    if data.get("slug") != "e2e-get-slug":
        failures.append(f"{label}.slug mismatch: {data.get('slug')!r}")
    if data.get("status") != "active":
        failures.append(f"{label}.status mismatch: {data.get('status')!r}")
    if data.get("id") != create_data.get("id"):
        failures.append(f"{label}.id {data.get('id')!r} != create.id {create_data.get('id')!r}")

if failures:
    for f in failures:
        print(f"FAIL: {f}", file=sys.stderr)
    sys.exit(1)

print("PASS: 12_cli_link_get")
PY
