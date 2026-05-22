# QA Section 09 — Error Handling

## Purpose
Validate malformed URL, invalid short code, duplicate record, and DB-unavailable behavior.

## Preconditions
- Fresh DB

## Required Components
- `qa/scripts/sections/09_error_handling.sh`
- `tools/sqlite_state_assert.py`

## Execution Steps
### Phase 1 — Reset Environment
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database
```

### Phase 2 — Start Services
```bash
bash qa/scripts/sections/09_error_handling.sh
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
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --expect qa/expected/09_error_handling.json
```

### Phase 6 — Verify Runtime State Again
```bash
python3 -m tools.sqlite_state_assert --assert-log-contains qa/tmp/server.log duplicate
```

### Phase 7 — Cleanup
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database
```
