# Stage 0 Specification — Ubuntu Dependency Baseline and CI Bootstrap

## 1. Stage intent

Establish a **simple, reproducible, package-manager-based development baseline** for the URL shortener project on Ubuntu.

This stage exists to remove the previous dependency strategy based on building third-party libraries from source, and replace it with a maintainable approach where:

1. All required build and test dependencies are installed through `apt`.
2. A single shell script prepares a fresh Ubuntu machine for local development and CI.
3. The application configures, compiles, and runs tests using only system packages.
4. GitHub Actions validates the same Ubuntu-first workflow automatically.

This stage is intentionally infrastructure-oriented. It must make all later stages easier to implement, test, and review.

---

## 2. Scope and non-goals

### 2.1 In scope

- Audit current build-time and test-time dependencies.
- Remove or deprecate Boost-from-source setup flow.
- Introduce Ubuntu setup script based on `apt install`.
- Ensure the project builds on a supported Ubuntu version using system packages only.
- Ensure all existing tests compile and run in that environment.
- Introduce GitHub Actions pipeline for configure/build/test on Ubuntu.
- Update developer documentation accordingly.
- Fail clearly when a required system dependency is unavailable.

### 2.2 Out of scope

- Cross-platform package manager support (Homebrew, vcpkg, Conan, etc.).
- Docker-based development environments.
- Production deployment automation.
- Multi-distro Linux support beyond a documented best effort.
- Refactoring business logic unrelated to buildability.
- Introducing new functional product features.

### 2.3 Hard constraints

- Do **not** compile Boost or other core third-party dependencies from source in the main setup path.
- Do **not** require manual dependency installation steps beyond invoking the provided script and standard build commands.
- Do **not** rely on interactive prompts in setup or CI.
- Do **not** introduce hidden dependency downloads during CMake configure/build, unless they are explicitly documented as test-only fallback and disabled by default.

---

## 3. Ubuntu support contract

### 3.1 Primary development target

Primary supported local and CI environment for this stage:

- **Ubuntu 24.04 LTS**

Secondary best-effort compatibility target (optional if current package versions permit without extra complexity):

- **Ubuntu 22.04 LTS**

The implementation must document which Ubuntu release is treated as canonical for CI and issue triage.

### 3.2 Compiler/toolchain baseline

Use Ubuntu-packaged toolchains.

Required baseline:

- `cmake`
- `ninja-build`
- `g++`
- `gcc`
- `git`
- `pkg-config`
- `make` (optional convenience, but acceptable to install)

If Clang is already supported or trivial to support with system packages, it may be added later, but GCC on Ubuntu is the required success path for this stage.

### 3.3 Dependency philosophy

For this project, dependencies must be classified and documented as:

- **Required runtime/build dependencies**
- **Required test dependencies**
- **Optional development tooling**
- **Future-stage dependencies not yet required**

This prevents Stage 0 from over-installing packages that are only needed in Stage 3+.

---

## 4. Dependency audit and package mapping

Create a documented dependency inventory for the current codebase and immediate test path.

### 4.1 Required categories to audit

Audit at minimum:

- build tools
- C++ compiler
- TLS/crypto libraries
- HTTP/networking libraries
- JSON library
- logging library
- unit test framework
- any existing benchmarking dependency
- any transitive dependency currently assumed to exist because of previous source builds

### 4.2 Expected Ubuntu package mapping

The exact package list may vary slightly depending on the current repository state, but the setup script must install system packages equivalent to the project's actual build graph.

Typical expected packages for current and near-term stages may include:

- `build-essential`
- `cmake`
- `ninja-build`
- `git`
- `pkg-config`
- `curl` (optional but useful for smoke examples)
- `ca-certificates`
- `libssl-dev`
- `zlib1g-dev` (only if actually needed)
- `nlohmann-json3-dev` (if system package is used instead of vendored header)
- `libspdlog-dev` (if spdlog is a direct dependency)
- `libgtest-dev` (if GoogleTest is used and linked in current project setup)
- appropriate Boost development packages, preferably minimized to the actual used modules

### 4.3 Boost policy

A major goal of this stage is to simplify Boost support.

Required policy:

- Prefer Ubuntu system packages instead of source builds.
- Install **only the Boost packages actually needed** by the project, if module-granular packages are practical on the target Ubuntu release.
- If Ubuntu packaging makes module-granular install impractical or brittle, using `libboost-all-dev` is acceptable for Stage 0 **provided this choice is documented as a simplicity-first baseline**.

The implementation must explicitly document which approach was selected and why.

### 4.4 Future dependency staging

Do not install Stage-3+ backend packages in the default Stage 0 bootstrap unless they are already required by the current build.

Examples that should remain deferred unless already necessary:

- PostgreSQL client/server packages
- Redis server/client packages
- database migration tools
- benchmark/reporting tools not used by current CI

If the project wants optional convenience install groups for future stages, they must be separate and clearly named.

---

## 5. Required deliverables

### 5.1 Ubuntu setup script

Create a dedicated script, for example:

- `scripts/setup_ubuntu_dependencies.sh`

The script must:

1. Use `bash` with strict mode:
   - `set -euo pipefail`
2. Detect Ubuntu version.
3. Refresh package metadata with `apt-get update`.
4. Install all required packages non-interactively.
5. Be safe to re-run.
6. Print clear progress steps.
7. Exit with non-zero status on failure.

Preferred installation style:

```bash
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y ...
```

The script may require `sudo` for local usage, but GitHub Actions usage must remain non-interactive.

### 5.2 Optional dry-run / grouping support

Recommended but not strictly mandatory:

- support flags such as:
  - `--base`
  - `--test`
  - `--dev-tools`
  - `--ci`
- or define arrays/groups internally and install the union needed for local dev + tests

If flags are added, they must remain simple and well documented.

### 5.3 Build verification script or documented command sequence

Provide one of the following:

- `scripts/build_and_test.sh`, or
- documented canonical command sequence in README + CI

Canonical build flow should look like:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

If tests require additional flags, they must be encoded consistently in both docs and CI.

### 5.4 CMake cleanup

Update build scripts so they resolve dependencies from the system installation path instead of assuming manually compiled third-party artifacts.

Required outcomes:

- no hard-coded local Boost source/build directories
- no implicit expectation of custom `BOOST_ROOT` unless optional and documented
- clear failure messages when a required package is missing
- package discovery through standard CMake mechanisms wherever reasonable

### 5.5 GitHub Actions pipeline

Create a workflow file, for example:

- `.github/workflows/ubuntu-build-and-test.yml`

Minimum required behavior:

1. Trigger on:
   - pull requests
   - pushes to main development branches
2. Use Ubuntu runner (canonical version should match §3.1 when available in GitHub-hosted runners).
3. Check out repository.
4. Install dependencies through the same shell script created for local setup, not a completely separate ad-hoc dependency list.
5. Configure project.
6. Build project.
7. Run tests.
8. Fail the workflow if any step fails.

Recommended workflow name:

- `ubuntu-build-and-test`

---

## 6. Repository and build-system requirements

### 6.1 Single source of truth for dependency installation

The package list used by GitHub Actions must come from the project repository and should be shared with local development.

Preferred patterns:

- GitHub Actions calls `scripts/setup_ubuntu_dependencies.sh`, or
- GitHub Actions sources a common package list file used by the script

Avoid duplicating package lists in multiple places.

### 6.2 CMake package discovery rules

Use standard CMake discovery for installed packages wherever possible.

Examples:

- `find_package(OpenSSL REQUIRED)`
- `find_package(Boost REQUIRED COMPONENTS ...)`
- `find_package(nlohmann_json REQUIRED)` if applicable
- `find_package(spdlog REQUIRED)` if applicable
- `find_package(GTest REQUIRED)` if applicable

If the current project vendors a header-only dependency and that is intentionally retained, document it clearly and do not install redundant system packages unless justified.

### 6.3 Minimize hidden build magic

Avoid fragile build behavior such as:

- fetching dependencies during configure by default
- custom path probing across developer home directories
- mixing vendored and system package discovery without explicit policy

If compatibility fallbacks exist, the precedence order must be documented.

### 6.4 Preserve simplicity for later stages

Stage 0 should not paint later stages into a corner.

The build system should remain ready for future optional dependencies introduced in later stages, but those dependencies must not become mandatory yet.

---

## 7. Documentation requirements

### 7.1 README update

Update `README.md` so a new developer on Ubuntu can bootstrap the project with a short, correct sequence.

Required content:

- supported Ubuntu version(s)
- command to install dependencies
- command to configure/build
- command to run tests
- note that old source-build flow for Boost is deprecated/removed
- note about optional future-stage dependencies being intentionally excluded from Stage 0

### 7.2 Developer documentation

Add or update a build/development document, for example:

- `docs/DEVELOPMENT.md`
- or `docs/setup/UBUNTU.md`

Recommended content:

- package groups explanation
- rationale for system-package strategy
- troubleshooting missing package names across Ubuntu releases
- how CI mirrors local workflow
- how to override compiler/build type if needed

### 7.3 Migration note

If the repository previously documented building Boost from source, add an explicit migration note stating:

- that path is no longer the default
- what changed
- how older instructions should be interpreted or removed

---

## 8. Functional and technical acceptance criteria

### 8.1 Local bootstrap acceptance

On a clean supported Ubuntu machine, the following must succeed:

1. clone repository
2. run dependency installation script
3. configure project
4. compile project
5. run tests successfully

No manual dependency compilation steps may be required.

### 8.2 CI acceptance

GitHub Actions must demonstrate the same success path using the same dependency setup script.

### 8.3 Build reproducibility acceptance

Re-running the dependency setup script and build commands should not require cleanup of manually created third-party trees.

### 8.4 Failure-path acceptance

If a required package is unavailable or package installation fails, the script must exit clearly and early with a readable error.

### 8.5 Dependency simplification acceptance

Repository documentation and build scripts must no longer present Boost-from-source as the recommended path for Ubuntu.

---

## 9. Testing requirements

### 9.1 Required verification tests for this stage

At minimum, verify:

- project configures successfully after system package installation
- main target builds successfully
- all existing automated tests run successfully
- CI invokes the setup script successfully

### 9.2 Recommended additional checks

Recommended if low-cost:

- a smoke step in CI launching the application binary with `--help` or equivalent safe startup path
- a validation step that prints resolved dependency versions during configure for easier diagnostics

### 9.3 What not to do in this stage

- Do not add brittle environment-dependent tests.
- Do not require running external services.
- Do not add packaging or deployment tests unless already trivial.

---

## 10. GitHub Actions specification

### 10.1 Workflow structure

Minimum workflow steps:

1. `actions/checkout`
2. install dependencies via script
3. configure with CMake + Ninja
4. build
5. run tests with `ctest --output-on-failure`

### 10.2 Caching

Caching is optional for this stage.

Do not add complex caching logic if it obscures correctness. Simplicity and reproducibility matter more than shaving small amounts of CI time at this point.

### 10.3 Logs and diagnostics

The workflow should make failures diagnosable.

Recommended:

- print compiler version
- print CMake version
- print Ubuntu release info
- retain straightforward command sequence in logs

---

## 11. Suggested implementation plan

Recommended execution order:

1. Audit current dependency graph and existing build instructions.
2. Remove or isolate legacy source-build assumptions.
3. Update CMake dependency discovery for system packages.
4. Create Ubuntu setup script.
5. Validate local configure/build/test.
6. Create GitHub Actions workflow using the same script.
7. Update README and development docs.
8. Re-run full clean bootstrap verification on Ubuntu.

---

## 12. Risks and mitigation

### Risk: Ubuntu package names differ by release

Mitigation:

- choose one canonical Ubuntu release for CI
- document any best-effort compatibility notes separately
- keep package selection conservative

### Risk: Current build scripts assume custom Boost paths

Mitigation:

- replace those assumptions with `find_package(Boost ...)`
- remove stale variables from docs and helper scripts

### Risk: GoogleTest packaging differences

Mitigation:

- verify whether Ubuntu target release provides readily usable CMake config/package targets
- if special handling is needed, document it explicitly and keep it minimal

### Risk: Future-stage backends may tempt over-installation now

Mitigation:

- install only what current compile/test path needs
- keep future backend dependencies for later stages or optional groups

---

## 13. Exit criteria

This stage is complete only when **all** of the following are true:

1. A fresh supported Ubuntu environment can install all required dependencies using the provided `apt`-based shell script.
2. No source build of Boost is required for the supported Ubuntu workflow.
3. The project configures successfully with CMake using system-installed packages.
4. The project compiles successfully.
5. The existing automated test suite passes.
6. A GitHub Actions workflow exists and performs dependency installation, build, and test on Ubuntu.
7. README/developer documentation reflects the new Ubuntu-first dependency setup.
8. Legacy Ubuntu instructions based on manual third-party source compilation are removed or clearly deprecated.

---

## 14. Implementation notes for the coding agent

- Favor the simplest maintainable Ubuntu dependency strategy over theoretical package minimalism.
- If choosing between a slightly larger package set and a fragile package-resolution scheme, prefer the more robust option and document the trade-off.
- Do not invent non-Ubuntu dependency managers in this stage.
- Keep scripts readable and operationally boring.
- Make CI mirror local development as closely as possible.
- Preserve a clean foundation for Stage 1+ without prematurely adding database or analytics dependencies.

