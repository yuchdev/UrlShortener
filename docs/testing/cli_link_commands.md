# QA Manual Test Checklist – CLI Link Commands

**Feature:** `link create` / `link get` / `link stats` one-shot CLI subcommands  
**Stage:** CLI command layer (app::LinkCommandService + main.cpp dispatch)  
**Last updated:** 2026-08-01  

---

## Environment / Fixtures Needed

| Item | Value |
|------|-------|
| Built binary | `cmake-build/Debug/url_shortener` (or `cmake-build/url_shortener`) |
| Working directory | Any writable directory; each test should use a **fresh temp dir** to avoid state bleed between `uri.txt` files |
| No network needed | All tests use `https://example.com` or similar; no live external service |
| No server needed | CLI mode must NOT require a running server |

---

## Prerequisites (implementation must be complete before manual QA)

- [ ] `ParseResult` carries a command variant: `server` | `link create` | `link get` | `link stats`
- [ ] `main.cpp` dispatches to CLI mode when first positional token is `link`
- [ ] CLI mode does NOT call `server.run()` or `io_context.run()`
- [ ] CLI mode loads `uri.txt` from current directory before executing the command
- [ ] CLI mode saves `uri.txt` to current directory on clean exit
- [ ] `src/app/LinkCommandService.cpp`, `src/app/LegacyAdapters.cpp`, `src/composition/LinkCommandServiceFactory.cpp` are in `COMMON_CPP_SOURCES` in `CMakeLists.txt`
- [ ] The `std::optional::or_else` call (C++23) in any refactored handler has been replaced with a C++17-compatible pattern

---

## Test Cases

### 1. `link create` – happy path

**Steps:**
```bash
cd $(mktemp -d)
./url_shortener link create \
    --url https://example.com/landing \
    --base-domain http://sho.rt
```

**Expected:**
- Exit code: `0`
- Stdout: single-line JSON  
  - `id` field: non-empty string  
  - `slug` field: non-empty 7-character alphanumeric string  
  - `url` field: `"https://example.com/landing"`  
  - `status` field: `"active"`  
  - `short_url` field starts with `"http://sho.rt/"`  
- No server port bound (verify with `ss -tln | grep :8000` → empty)

---

### 2. `link create` – explicit slug

**Steps:**
```bash
cd $(mktemp -d)
./url_shortener link create \
    --url https://example.com/promo \
    --slug summer25
```

**Expected:**
- Exit code: `0`
- JSON `slug == "summer25"`
- `uri.txt` present in cwd after exit

---

### 3. `link create` then `link get` – state persistence

**Steps:**
```bash
TMPD=$(mktemp -d)
cd "$TMPD"
./url_shortener link create --url https://persist.example.com --slug persist-test
./url_shortener link get --slug persist-test
```

**Expected:**
- Both commands exit `0`
- `link get` stdout JSON: `slug == "persist-test"`, `url == "https://persist.example.com"`

---

### 4. `link get` by id

**Steps:**
```bash
TMPD=$(mktemp -d)
cd "$TMPD"
ID=$(./url_shortener link create --url https://byid.example.com --slug byid-test | python3 -c "import json,sys; print(json.load(sys.stdin)['id'])")
./url_shortener link get --id "$ID"
```

**Expected:**
- Both commands exit `0`
- `link get` stdout JSON: `id == $ID`, `url == "https://byid.example.com"`

---

### 5. `link get` – not found

**Steps:**
```bash
cd $(mktemp -d)
./url_shortener link get --slug doesnotexist
```

**Expected:**
- Exit code: non-zero (1)
- No JSON with `status == "active"` on stdout
- Error message on stderr or stdout indicating "not found"

---

### 6. `link create` – invalid URL (SSRF guard)

**Steps:**
```bash
cd $(mktemp -d)
./url_shortener link create --url "not-a-url"
./url_shortener link create --url "ftp://files.example.com"
./url_shortener link create --url "javascript:alert(1)"
./url_shortener link create --url "http://10.0.0.1/internal"
```

**Expected (each):**
- Exit code: non-zero
- No `uri.txt` created (or existing `uri.txt` unchanged)

---

### 7. `link create` – reserved slug

**Steps:**
```bash
cd $(mktemp -d)
./url_shortener link create --url https://example.com --slug health
./url_shortener link create --url https://example.com --slug api
./url_shortener link create --url https://example.com --slug ADMIN
```

**Expected (each):**
- Exit code: non-zero
- Error message mentions "reserved" or the error code

---

### 8. `link create` – duplicate slug conflict

**Steps:**
```bash
TMPD=$(mktemp -d)
cd "$TMPD"
./url_shortener link create --url https://example.com/one --slug dup-slug
./url_shortener link create --url https://example.com/two --slug dup-slug
```

**Expected:**
- First invocation: exit `0`
- Second invocation: exit non-zero, error mentions "conflict" or "already in use"

---

### 9. `link stats` – invalid window

**Steps:**
```bash
cd $(mktemp -d)
# from > to
./url_shortener link stats --slug any --from 1800000000 --to 1700000000 --bucket day
# unsupported bucket
./url_shortener link stats --slug any --from 1700000000 --to 1800000000 --bucket minute
```

**Expected (each):**
- Exit code: non-zero
- Error message indicates the invalid field

---

### 10. Separation – CLI does not start server

**Steps:**
```bash
# Verify port 8000 is free first
ss -tln | grep :8000   # should be empty

cd $(mktemp -d)
./url_shortener link create --url https://example.com

# Immediately after exit:
ss -tln | grep :8000   # must still be empty
```

**Expected:**
- Port 8000 is NOT open after `link create` exits
- Process exits promptly (< 2 seconds)

---

### 11. `--help` still works in server mode

**Steps:**
```bash
./url_shortener --help
```

**Expected:**
- Exit code: `0`
- Usage message printed
- No server started

---

### 12. Server mode still works after CLI additions

**Steps:**
```bash
./url_shortener --http-port 28999 --tls-enabled false &
SERVER_PID=$!
sleep 1.5
curl -fsS http://localhost:28999/health
kill $SERVER_PID
```

**Expected:**
- `/health` returns HTTP 200 (server mode not broken by CLI refactoring)

---

## Auth / Admin paths to verify

None required for the initial CLI scope (create + read + stats only).  
Auth-gated CLI commands (if added in a later stage) should verify:
- Unauthenticated invocation is rejected with a clear error message
- Token/credential arguments are accepted and validated

---

## Artifacts on failure

If a test step fails, capture:
```bash
# stderr for the failing command
./url_shortener link create ... 2>fail.stderr
# contents of uri.txt if present
cat uri.txt 2>/dev/null || echo "(no uri.txt)"
```
