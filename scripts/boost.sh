#!/usr/bin/env bash
set -euo pipefail

echo "ERROR: Building Boost from source is deprecated for Ubuntu Stage 0 baseline." >&2
echo "Use apt packages instead by running: scripts/setup_ubuntu_dependencies.sh" >&2
exit 1
