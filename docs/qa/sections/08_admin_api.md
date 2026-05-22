# QA Section 08 — Admin API

## Purpose
Validate admin API visibility for URL and analytics records.

## Preconditions
- Fresh DB
- Service started

## Required Components
- `qa/scripts/sections/08_admin_api.sh`
- `tools/sqlite_state_assert.py`

## Execution Steps
### Phase 1 — Reset Environment
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database
```

### Phase 2 — Start Services
```bash
bash qa/scripts/sections/08_admin_api.sh
```

### Phase 3 — Verify Runtime State
```bash
python3 -m tools.sqlite_state_assert --assert-port-open 18080
```

### Phase 4 — Execute Actions
```bash
curl -fsS http://127.0.0.1:18080/admin/stats
```

### Phase 5 — Verify Database State
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --expect qa/expected/08_admin_api.json
```

### Phase 6 — Verify Runtime State Again
```bash
python3 -m tools.sqlite_state_assert --assert-http-body-contains http://127.0.0.1:18080/admin/stats total
```

### Phase 7 — Cleanup
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database
```
