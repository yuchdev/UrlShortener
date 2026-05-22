# QA Section 02 — Create Short URL

## Purpose
Validate create flow and collision handling.

## Preconditions
- Fresh DB
- Service started

## Required Components
- `qa/scripts/sections/02_create_short_url.sh`
- `tools/sqlite_state_assert.py`

## Execution Steps
### Phase 1 — Reset Environment
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database
```

### Phase 2 — Start Services
```bash
bash qa/scripts/sections/02_create_short_url.sh
```

### Phase 3 — Verify Runtime State
```bash
python3 -m tools.sqlite_state_assert --assert-port-open 18080
```

### Phase 4 — Execute Actions
```bash
curl -fsS http://127.0.0.1:18080/r/demo
```

### Phase 5 — Verify Database State
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --expect qa/expected/02_create_short_url.json
```

### Phase 6 — Verify Runtime State Again
```bash
python3 -m tools.sqlite_state_assert --assert-http-status http://127.0.0.1:18080/r/demo 302
```

### Phase 7 — Cleanup
```bash
python3 -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database
```
