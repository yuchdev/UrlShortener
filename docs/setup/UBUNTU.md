# Ubuntu setup (Stage 0 baseline)

## Supported baseline

- Canonical: **Ubuntu 24.04 LTS**
- Best effort: Ubuntu 22.04 LTS

CI uses Ubuntu 24.04 and should be treated as the authoritative environment for Stage 0 triage.

## Dependency strategy

Stage 0 uses Ubuntu system packages (`apt`) only. Building Boost from source is no longer part of the supported Ubuntu workflow.

## Install dependencies

From repository root:

```bash
./scripts/setup_ubuntu_dependencies.sh
```

The script installs:

- Required build/runtime dependencies:
  - `build-essential`
  - `cmake`
  - `ninja-build`
  - `git`
  - `pkg-config`
  - `libssl-dev`
  - `libboost-all-dev`
  - `ca-certificates`
- Required test dependencies:
  - `python3`
- Optional convenience tooling:
  - `curl`

### Why `libboost-all-dev` in Stage 0

For Stage 0, we prioritize a simple and robust Ubuntu bootstrap over aggressive package minimization. `libboost-all-dev` avoids release-to-release package naming friction and keeps CI/local setup aligned.

## Configure, build, test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target url_shortener
ctest --test-dir build --output-on-failure
```

## Migration note (from old Boost source-build flow)

- `scripting/boost.sh` and `scripting/boost.cmd` are deprecated for Stage 0 Ubuntu setup.
- `cmake/fetch-boost.cmake` is deprecated and no longer supported in the Ubuntu path.
- Use the apt bootstrap script instead.

## CI parity

GitHub Actions workflow `.github/workflows/ubuntu-ci.yml` runs the same dependency script and build/test commands used locally.
