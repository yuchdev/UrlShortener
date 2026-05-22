#!/usr/bin/env bash
set -euo pipefail

DEFAULT_SECTIONS=(
  01_server_boot
  02_create_short_url
  03_redirects
  04_expiration
  05_fingerprinting
  06_authentication
  07_permissions
  08_admin_api
  09_error_handling
  10_concurrency
)

if [[ -n "${QA_SECTIONS:-}" ]]; then
  IFS=',' read -r -a SECTIONS <<< "${QA_SECTIONS}"
else
  SECTIONS=("${DEFAULT_SECTIONS[@]}")
fi

mkdir -p qa/failures
SUMMARY="qa/failures/summary.txt"
: > "$SUMMARY"

for section in "${SECTIONS[@]}"; do
  echo "Running ${section}"
  if bash qa/scripts/run_section.sh "$section"; then
    echo "${section}: PASS" | tee -a "$SUMMARY"
  else
    echo "${section}: FAIL" | tee -a "$SUMMARY"
    exit 1
  fi
done

echo "All selected sections passed." | tee -a "$SUMMARY"
