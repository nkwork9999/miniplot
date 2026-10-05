# Improvement progress

Target: 100 independently reviewable improvements. Completed in this batch: 15. The target is not complete. Test cases and formatting edits are not counted separately.

1. HTML titles use HTML entity encoding in every chart renderer.
2. JavaScript strings escape all ASCII control bytes, including NUL.
3. Chart doubles retain round-trip precision.
4. Chart numbers use the classic decimal locale.
5. Scatter x labels retain double precision.
6. NULL array elements are rejected instead of silently losing alignment.
7. Non-finite coordinates are rejected before generating JavaScript.
8. File write/close failures return an IOException.
9. Lowercase conversion handles unsigned characters safely.
10. All 18 chart scalar APIs process every row in an input chunk.
11. NULL scalar arguments produce NULL results per row.
12. HTML helpers can be tested independently of DuckDB.
13. A portable standalone regression runner validates encoding and rendering.
14. Linux and macOS CI run the standalone regressions.
15. Test documentation describes the actual extension and regression commands.

## Validation

Standalone C++ HTML regressions passed; full extension translation unit passed clang++ syntax checking against the pinned miniplot DuckDB headers. Added SQL regressions have not yet been run in a built DuckDB extension.
