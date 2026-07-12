# QA Section 05 — Fingerprinting

## Purpose
Validate new, repeated, and suspicious fingerprint handling.

## Preconditions
- Fresh DB

## Required Components
- `tests/e2e/scripts/sections/05_fingerprinting.sh`
- `tools/sqlite_state_assert.py`

## Execution Steps
### Phase 1 — Reset Environment
```bash
python3 -m tools.sqlite_state_assert --db tests/e2e/tmp/qa.sqlite --reset-database
```

### Phase 2 — Start Services
```bash
bash tests/e2e/scripts/sections/05_fingerprinting.sh
```

### Phase 3 — Verify Runtime State
```bash
python3 -m tools.sqlite_state_assert --assert-port-open 18080
```

### Phase 4 — Execute Actions
```bash
curl -fsS http://127.0.0.1:18080/health
```

### Phase 5 — Verify Database State
```bash
python3 -m tools.sqlite_state_assert --db tests/e2e/tmp/qa.sqlite --expect tests/e2e/expected/05_fingerprinting.json
```

### Phase 6 — Verify Runtime State Again
```bash
python3 -m tools.sqlite_state_assert --assert-log-contains tests/e2e/tmp/server.log suspicious
```

### Phase 7 — Cleanup
```bash
python3 -m tools.sqlite_state_assert --db tests/e2e/tmp/qa.sqlite --reset-database
```
