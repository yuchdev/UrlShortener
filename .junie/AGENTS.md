# AGENTS Notes for UrlShortener

## Build & Configuration (project-specific)

- This project requires a vcpkg toolchain at configure time; `CMakeLists.txt` fails fast if neither `CMAKE_TOOLCHAIN_FILE` nor `VCPKG_ROOT` is set.
- In this workspace, use the existing CLion CMake profile and build directory only:
  - Profile: `Debug-Visual Studio`
  - Build dir: `C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio`
- Targeted build command pattern (avoid full rebuilds when iterating on one area):

```powershell
cmake --build "C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio" --target <target_name> --config Debug
```

- Main app target:

```powershell
cmake --build "C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio" --target url_shortener --config Debug
```

## Testing workflow

### Running tests

- Tests are registered in root `CMakeLists.txt` with `add_test(...)` and grouped by labels (`unit`, `integration`, `contract`).
- Use `ctest --test-dir` against the existing build directory (do not run from repo root without `--test-dir`).
- Run a single test by exact name:

```powershell
ctest --test-dir "C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio" -C Debug -R "^<test_name>$" --output-on-failure
```

- Run by label:

```powershell
ctest --test-dir "C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio" -C Debug -L unit --output-on-failure
```

### Verified example (executed)

The following sequence was validated in this environment:

```powershell
cmake --build "C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio" --target 01_url_validation_accept_test --config Debug
ctest --test-dir "C:\Users\atatat\Projects\UrlShortener\cmake-build-debug-visual-studio" -C Debug -R "^01_url_validation_accept_test$" --output-on-failure
```

Observed result: `100% tests passed, 0 tests failed out of 1`.

### Adding new tests

- Add a new `*.cpp` test file in the appropriate suite directory (`tests/unit/...`, `tests/integration/...`, or `tests/contract/...`).
- Register it by appending the file path to the corresponding source list in `CMakeLists.txt`.
- Naming behavior is not uniform:
  - `CORE_BASIC_UNIT_SOURCES` and `LINK_MANAGEMENT_UNIT_SOURCES` produce test targets equal to filename stem.
  - Most other suites are registered via `register_labeled_cpp_tests(...)` and produce targets/tests named `<parent_dir>__<filename_stem>`.
- After adding, build just that target and run via exact-name `ctest -R "^...$"`.

## Additional development notes

- Keep C++ standard compatibility at C++17.
- Prefer small, targeted builds and test runs due the large number of executable test targets.
- Repository is modularized under `src/storage`, `src/analytics`, `src/http`, `src/observability`, `src/core`, `src/security`, `src/config`, and `src/composition`; preserve existing local style in touched module.
- Existing codebase conventions to keep:
  - explicit, descriptive test names with numeric prefixes (`01_...`, `02_...`) for order/readability;
  - CMake lists grouped by subsystem and then registered centrally.
