#!/usr/bin/env bash
set -euo pipefail

SECTION="${1:?usage: bash qa/scripts/run_section.sh <section_name>}"
SCRIPT="qa/scripts/sections/${SECTION}.sh"

if [[ ! -x "$SCRIPT" ]]; then
  echo "Missing section script: $SCRIPT" >&2
  exit 2
fi

bash "$SCRIPT"
