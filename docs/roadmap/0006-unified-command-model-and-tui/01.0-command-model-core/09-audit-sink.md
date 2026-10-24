# 09 - Audit sink

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/07
**Blocks:** 13, 03.0/18
**Estimated size:** S
**Suggested agent:** cpp-expert (review: security-auditor)

## Objective

Emit exactly one audit record per attempted execution of a *mutating* command
(principal, transport, command, target slug/id, outcome) through an injected
`IAuditSink`, and ship a logger-backed implementation. Argument values never
appear in a record (ADR 0002 "Authorization", milestone C6).

## Dependencies

- 01.0/07 pipeline (the hook points are its early returns).
- `url_shortener::observability::Logger` / `LogField` (verified in
  `include/url_shortener/observability/logger.hpp`; `info(msg, std::vector<LogField>)`)
  for the production sink. Logger initialisation is the caller's job
  (`LoggerFactory::Initialize`); the sink must work when it was not
  initialised (UNVERIFIED how `Logger` behaves uninitialised; the test does not
  depend on it).
- `CommandResult::audit_target` (06) supplies the slug/id.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/audit.hpp` | create | `AuditRecord`, `IAuditSink`, `LoggerAuditSink`, `audit_fields` |
| `src/commands/audit.cpp` | create | `audit_fields`, `LoggerAuditSink` |
| `include/url_shortener/commands/command_registry.hpp` | modify | `set_audit_sink`, member |
| `src/commands/command_registry.cpp` | modify | Emit records in `execute` |
| `tests/unit/commands/09_audit.cpp` | create | Tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/commands/audit.cpp`; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

```cpp
namespace url_shortener::commands {

struct AuditRecord
{
    std::string principal;
    std::string transport;
    std::string command;
    std::string target;     // slug / id / job id; empty when unknown; length-capped (64)
    std::string outcome;    // "ok", "dry_run", "job_accepted", or the error code name
};

class IAuditSink
{
public:
    virtual ~IAuditSink() = default;
    virtual void record(const AuditRecord& record) noexcept = 0;
};

/// Pure, testable: {"event":"command_audit", principal, transport, command, target, outcome}
std::vector<observability::LogField> audit_fields(const AuditRecord& record);

class LoggerAuditSink final : public IAuditSink
{
public:
    explicit LoggerAuditSink(observability::Logger logger);   // Component("audit") in composition
    void record(const AuditRecord& record) noexcept override;
private:
    observability::Logger m_logger;
};

// CommandRegistry (additions)
void set_audit_sink(std::shared_ptr<IAuditSink> sink);   // null = auditing off (unit tests only;
                                                         // every production runtime installs a
                                                         // LoggerAuditSink in composition::build_command_runtime,
                                                         // Task 03.0 subtask 18)
}
```

Emission rules in `execute` (only when `descriptor.mutating` and a sink is set):

- `not_authorized` (step 3), `invalid_field` (step 4) -> record with the error
  code as outcome and empty target (attempted mutation by that principal);
- `unsupported` from option handling, `busy` from the submitter -> error code;
- handler success -> `ok` (`dry_run` when `ctx.dry_run`), target =
  `CommandResult::audit_target`;
- job accepted -> `job_accepted`, target = job id;
- handler failure -> the error code, target from the result;
- exception in handler -> `internal`.
- `unknown_command` and non-mutating commands emit nothing (an unknown name
  has no descriptor to say it is mutating; reads are not audited by this
  mechanism).

Principal and transport come from `CallerContext`; `audit_fields` truncates
every field to 128 characters and replaces control characters with `?` (log
injection).

## Implementation steps

1. Add `audit.hpp/.cpp`.
2. Add `m_audit` member and `set_audit_sink`; implement a private
   `emit_audit(descriptor, caller, outcome, target)` helper; call it from each
   exit of `execute` after the lookup succeeded.
3. Ensure the helper is called exactly once per execution (single-exit
   structure or a scope-guard `finish(response)`), and that sink exceptions
   cannot occur (`noexcept` interface; wrap the call in `try/catch(...)` anyway).
4. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__09_audit
ctest --test-dir cmake-build -R "^commands__09_audit$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/09_audit.cpp` | `commands__09_audit` | unit | below |

Uses a `CapturingAuditSink` fake.

- Mutating command success: one record, outcome `ok`, target = handler's
  `audit_target`, principal/transport from the caller.
- Denied (`User` calling a write command): one record `not_authorized`,
  handler not called.
- Invalid args: one record `invalid_field`.
- Dry run -> `dry_run`; accepted job -> `job_accepted` with job id; `busy` ->
  `busy`; handler exception -> `internal`.
- Read-only command and unknown command: zero records.
- **No value leakage**: request args contain `"password":"hunter2-SECRET"`-like
  values (as extra arg in a descriptor that has it) and a token-looking slug is
  not copied; serialize every captured record field and assert the secret
  string appears nowhere.
- `audit_fields`: expected keys; 300-char principal truncated to 128; newline
  and ESC characters sanitized.
- No sink set: mutating commands run normally; null sink after non-null
  disables emission.
- `LoggerAuditSink::record` does not throw for empty and oversized records
  (smoke only).

## Constraints

- Records contain no argument values and no raw client identifiers (C6);
  `audit_target` is set by handlers from a validated slug/id only.
- Exactly one record per execution (test counts), never zero for a mutating
  command that reached a descriptor.
- Sink is `noexcept`; auditing failure must never change the response.
- Do not log secrets, tokens or the analytics salt (CLAUDE.md).
- Milestone contracts: C6, C7.

## Success criteria

1. `commands__09_audit` passes.
2. The no-leakage test fails if a handler or the registry copies any arg value
   into a record (verified by temporarily adding such a copy locally).
3. Record count equals execution count for mutating commands across all
   outcomes listed above.

## Out of scope / risks

- Persisting audit to the DB (`auth_audit_log` is the auth subsystem's table;
  command audit goes to the application log in this milestone).
- Choosing the audit log destination/rotation (observability config).
- Reads are not audited; if `db.rows` access auditing becomes a requirement it
  is an additive `audited` descriptor flag.
