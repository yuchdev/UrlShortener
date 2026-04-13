# PostgreSQL Local Development

## Prerequisites

Install PostgreSQL client development packages so SOCI PostgreSQL backend can build:

- Ubuntu/Debian: `libpq-dev`
- plus project baseline dependencies from `scripts/setup_ubuntu_dependencies.sh`

## Example test DSN

Export a DSN for contract/integration tests:

```bash
export URL_SHORTENER_TEST_POSTGRES_DSN="dbname=url_shortener_test user=postgres host=127.0.0.1 port=5432"
```

## Run tests

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L contract --output-on-failure
ctest --test-dir build -L integration --output-on-failure
```
