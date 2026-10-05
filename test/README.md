# Regression tests

Run `bash scripts/test-unit.sh` from the repository root. The tests compile the
production C++ helpers without downloading DuckDB or browser libraries.
The tests cover all chart types, title escaping, script termination, control
bytes, double precision, and decimal locales. DuckDB SQL tests under `test/sql`
require the extension build and run with `make test`.
