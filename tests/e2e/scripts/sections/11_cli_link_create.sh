#!/usr/bin/env bash
# tests/e2e/scripts/sections/11_cli_link_create.sh
#
# E2E CLI section 11: link create emits valid JSON with expected fields.
#
# PREREQUISITES (must be implemented before this section passes):
#   1. CLI subcommand dispatch in main.cpp: "link create" branch.
#   2. link create --url --slug --base-domain arguments parsed correctly.
#   3. Process exits 0 on success, prints single JSON object to stdout.
#
# Does NOT use the mock HTTP service or SQLite state assertions.
# Uses a temp directory for process isolation (uri.txt scoped per test).
set -euo pipefail

BINARY="${URLSHORTENER_BIN:-}"
if [[ -z "$BINARY" ]]; then
  # Fallback heuristic: search common build paths from repo root.
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

echo "=== 11_cli_link_create: running link create ==="

OUTPUT="$(
  "$BINARY" link create \
    --url https://e2e-create.example.com \
    --slug e2e-create-slug \
    --base-domain http://sho.rt \
    2>/dev/null
)"

echo "stdout: $OUTPUT"

# Validate output is JSON with required fields using Python.
python3 - "$OUTPUT" <<'PY'
import json, sys

raw = sys.argv[1]
try:
    data = json.loads(raw)
except json.JSONDecodeError as exc:
    print(f"FAIL: stdout is not valid JSON: {exc}", file=sys.stderr)
    print(f"raw output: {raw!r}", file=sys.stderr)
    sys.exit(1)

failures = []

if data.get("slug") != "e2e-create-slug":
    failures.append(f"slug: expected 'e2e-create-slug', got {data.get('slug')!r}")

if data.get("url") != "https://e2e-create.example.com":
    failures.append(f"url: expected 'https://e2e-create.example.com', got {data.get('url')!r}")

if data.get("status") != "active":
    failures.append(f"status: expected 'active', got {data.get('status')!r}")

short_url = data.get("short_url", "")
if not short_url.startswith("http://sho.rt/"):
    failures.append(f"short_url: expected 'http://sho.rt/...' prefix, got {short_url!r}")

if not data.get("id"):
    failures.append("id: field is missing or empty")

if failures:
    for f in failures:
        print(f"FAIL: {f}", file=sys.stderr)
    sys.exit(1)

print("PASS: 11_cli_link_create")
PY
