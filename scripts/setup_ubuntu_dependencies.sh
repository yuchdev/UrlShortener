#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  SUDO="sudo"
else
  SUDO=""
fi

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This script requires apt-get (Ubuntu/Debian)." >&2
  exit 1
fi

readonly PACKAGES=(
  build-essential
  cmake
  ninja-build
  git
  pkg-config
  ca-certificates
  curl
  libssl-dev
  libboost-all-dev
  libpq-dev
  python3
)

echo "Updating apt package index..."
${SUDO} apt-get update -y

echo "Installing required packages..."
${SUDO} DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${PACKAGES[@]}"

echo "Installed packages: ${PACKAGES[*]}"
echo "Dependency setup complete."
