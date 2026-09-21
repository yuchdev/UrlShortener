# 01 - Windows backend table and DPAPI tradeoffs

**Parent task:** 07.0 Documentation
**State:** ⬜ Not started
**Depends on:** none (documents Tasks 01.0-03.0, 05.0 once implemented)
**Blocks:** none

## Objective

Publish the Windows backend table and usage examples, and the security
tradeoff writeup between DPAPI CurrentUser and LocalMachine scope - the
first two items of plan.md's "Documentation updates" list.

## Files

- `docs/security/windows-secret-backends.md` (new)

## Content

### Windows backend table and examples

Reproduce, as shipped documentation (not planning prose), the backend table
and CLI examples from plan.md's Goal/CLI contract sections:

```powershell
secret-store.exe --backend dpapi-user <command> ...
secret-store.exe --backend dpapi-machine <command> ...
secret-store.exe --backend wincred <command> ...
```

| CLI value | Windows backend | Intended scope |
|---|---|---|
| `dpapi-user` | Windows DPAPI CurrentUser protection | Per-user local secrets; default for developer and desktop use |
| `dpapi-machine` | Windows DPAPI LocalMachine protection | Machine-wide service secrets for Windows services running under different accounts |
| `wincred` | Windows Credential Manager | User-visible credential entries managed through Windows credential APIs |

Note that `dpapi-user` is the default when `--backend` is omitted (Task
05.0), and document the exact diagnostic output that reports the selected
backend.

### DPAPI CurrentUser-vs-LocalMachine tradeoffs

- CurrentUser (`dpapi-user`): ciphertext decryptable only under the same
  Windows user profile that encrypted it. Narrowest trust scope; recommended
  default.
- LocalMachine (`dpapi-machine`): ciphertext decryptable by any local account
  on the same machine. Broader trust scope than CurrentUser - **must** be
  paired with restrictive filesystem ACLs on the persisted ciphertext (Task
  03.0 Subtask 02), since LocalMachine DPAPI alone does not protect against
  other local accounts on the same host, only against copying the ciphertext
  to a different machine.
- Guidance: use `dpapi-user` unless the secret must be readable by a Windows
  service running under a different account than the one that stored it; in
  that case use `dpapi-machine` and document the ACL requirement for
  operators deploying the service.

## Constraints

- Describe only behavior implemented and tested by Tasks 01.0-03.0 and 05.0
  - do not document adapter internals (e.g. exact Win32 flags) beyond what
  an operator/developer needs to use the CLI and understand the trust-scope
  tradeoff.

## Success criteria

- [ ] `docs/security/windows-secret-backends.md` exists with the backend
      table, CLI examples, and the CurrentUser-vs-LocalMachine tradeoff
      section.
- [ ] The doc matches actually-implemented `--backend` values and default
      behavior (verified against Tasks 01.0/05.0 once complete).
