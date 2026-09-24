---
name: document-tests
description: User-invoked as /document-tests [path]. Documents every Boost.Test case (BOOST_AUTO_TEST_CASE / BOOST_FIXTURE_TEST_CASE) under path (default tests/) - classifies each as Unit, Fake, Integration, or E2E and inserts a standardized Scenario/Boundaries/On-failure Doxygen comment by delegating to the test-documenter agent. Use to bring an undocumented or partially-documented test suite up to a consistent standard.
allowed-tools: Read, Grep, Glob, Bash, Agent
invocation: /document-tests [path]
---

# Document Tests

Document every `BOOST_AUTO_TEST_CASE` / `BOOST_FIXTURE_TEST_CASE` case under
`$ARGUMENTS` (default `tests/`) with a standardized classification +
Scenario/Boundaries/On-failure Doxygen comment.

> **Note:** the reference project had an AST-based Python codemod
> (`scripts/document_tests.py`) that inserted the comments mechanically. There is
> **no equivalent codemod in this repo** (the stdlib has no C++ parser), so this
> skill is fully agent-driven: it delegates to the `test-documenter` agent, which
> reads each case and inserts the template by hand. Building a libclang-based C++
> codemod is a tracked follow-up.

## Steps

1. **Inventory**: list the Boost.Test cases in scope so the agent (and you) know
   the size of the job:
   ```bash
   grep -rnE "BOOST_(AUTO|FIXTURE)_TEST_CASE\(" $ARGUMENTS
   ```
   Note any file that already carries a `/** @test ... */` doc comment - those are
   left untouched by default.
2. **Delegate** to the **`test-documenter`** agent with the target path and the
   inventory. It classifies each case (Unit / Fake / Integration / E2E - refined by
   test directory, filename suffix, and body signals such as fakes/mocks or a
   spawned subprocess), inserts the fixed Doxygen template above each case,
   resolves ambiguous cases by reading the body, and leaves existing hand-written
   comments in place unless the user asked to standardize a specific file.
3. **Verify no regressions**: the comments are non-executable, but confirm the
   affected test target(s) still compile and run - a broken insertion shows up as a
   compile error, not a passing/failing test, so this check is cheap:
   ```bash
   cmake --build --preset <preset> --target <affected target>
   ctest --test-dir <build> -C Debug -R <name-or-label> --output-on-failure
   ```
4. Report the summary the agent produced.

## Output

```
## Test Documentation - <path>
Scanned: N files, M cases
[Unit] a  [Fake] b  [Integration] c  [E2E] d
Documented: X new/updated
Skipped (existing doc comment, left as-is): <list, or none>
Ambiguous - resolved: <file::case -> classification>
Verification: <build/ctest command> -> <pass/fail>
```

If the build fails after insertion, stop and hand the failure to `cpp-expert`
before re-running this skill - do not re-apply comments over a file that no longer
compiles. If the user wants existing hand-written comments standardized too, re-run
with the `test-documenter` agent and explicit permission naming the specific files.

## Completion checklist

- [ ] Inventory (`BOOST_AUTO_TEST_CASE` / `BOOST_FIXTURE_TEST_CASE`) run before any edit - existing-comment files identified
- [ ] Every case classified by the outermost boundary it exercises (E2E > Integration > Fake > Unit)
- [ ] Every existing hand-written comment left untouched, unless the user explicitly asked to override a named file
- [ ] Affected test target compiles and the targeted `ctest` selection passes after insertion
- [ ] Diff is Doxygen comments only - no test logic, fixtures, assertions, macro arguments, or includes changed
- [ ] Classification counts, skipped list, and ambiguous resolutions all included in the final report
