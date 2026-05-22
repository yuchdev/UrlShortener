# QA Section 01 — Server Lifecycle

## Purpose
Validate start, stop, restart, and double-start prevention behavior.

## Preconditions
- Fresh database at `qa/tmp/qa.sqlite`
- Python 3 available

## Required Components
- mock QA service (`qa/scripts/mock_service.py`)
- `tools/sqlite_state_assert.py`

## Execution Steps

### Phase 1 — Reset Environment
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database
```

### Phase 2 — Start Services
```bash
bash qa/scripts/sections/01_server_boot.sh
```

### Phase 3 — Verify Runtime State
```bash
python3 -m tools.sqlite_state_assert --assert-port-open 18080
python3 -m tools.sqlite_state_assert --assert-process-running mock_service.py
```

### Phase 4 — Execute Actions
```bash
curl -fsS http://127.0.0.1:18080/health
```

### Phase 5 — Verify Database State
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --expect qa/expected/01_server_boot.json
```

### Phase 6 — Verify Runtime State Again
```bash
python3 -m tools.sqlite_state_assert --assert-http-body-contains http://127.0.0.1:18080/health OK
```

### Phase 7 — Cleanup
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database
```
