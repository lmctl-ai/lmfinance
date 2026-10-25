# lmfinance durable memory

`lmfinance` is a static, dependency-free lmscript `_dsl_` host embedding QuantLib for financial instruments, yield curves, Black-Scholes/Merton pricing, and Monte Carlo risk simulations.
Repository: https://github.com/lmctl-ai/lmfinance.

## 60-second takeover

```sh
cmake -S . -B build                   # Configure CMake build
cmake --build build --parallel 4      # Build binary at build/lmfinance
./test.sh                             # Run full test suite (18 checks, gallery benchmarks)
```

## Document map

| File | Contents |
| --- | --- |
| [01-purpose-and-place.md](01-purpose-and-place.md) | Scope, architecture, and relation to sibling math/plot hosts |
| [02-libraries-and-licenses.md](02-libraries-and-licenses.md) | QuantLib and Boost header integration, licenses, clean-room build |
| [03-schema-v0.md](03-schema-v0.md) | Six financial namespaces, function signatures, instrument types |
| [04-numerics-and-validation.md](04-numerics-and-validation.md) | Textbook valuation benchmarks, analytic vs numeric tolerances |
| [05-build-run-test.md](05-build-run-test.md) | Measured build times, test.sh checks, static link policies |
| [06-ci.md](06-ci.md) | GitHub Actions CI matrix (5 platforms) and release pipeline |
| [07-examples.md](07-examples.md) | DSL financial examples, yield curve calibration, option pricing |
| [08-open-items.md](08-open-items.md) | Backlog, missing financial structures, roadmap |
| [09-resource-limits.md](09-resource-limits.md) | Memory budget allocation (`LMFINANCE_MAX_MEMORY_MB`) |
| [10-linux-glibc.md](10-linux-glibc.md) | System glibc baseline (>= 2.35), symbol allowlist, verification |
| [portable-gallery.md](portable-gallery.md) | Portable numeric markers and gnuplot stats integration |

## SDK pin and sync

The vendored SDK is pinned via `sdk.lock` (lmcode-cpp `ee1f084`) with a sha256 per file: `cpp/dslvm.hpp`, `cpp/stdlib/*.hpp`, `cpp/std/lmcode-std.schema.json`, `scripts/merge-std.py`.
The standard library (string, math, collections, file) is built into lmscript 0.2.420 and registered by the default `dslvm::Registry`, so the host schema declares only lmfinance members; `test.sh` excludes the 72 standard signatures from the registry before comparing with the compiler.
```sh
scripts/sync-sdk.sh                   # Refetches the pinned SDK headers from ../lmcode-cpp (LMCODE_CPP) and rewrites sdk.lock
scripts/sync-sdk.sh --verify          # Checks every pinned file
```

## Adding an example or function

### Adding an example
1. Create `examples/<name>.lms` using `_dsl_ cli="lmfinance" install="github:lmctl-ai/lmfinance@latest"`.
2. Run script: `build/lmfinance run examples/<name>.lms` or via `lmscript`.
3. Add the example to `./test.sh` test run list.
4. Save visual output to `docs/images/<name>.png` and update `README.md` demo table with `lmctl autorun` command.

### Adding a host function
1. Declare function/method in `schema/lmfinance.dsl.json`.
2. Register and implement the function in C++ source under `src/` using `dslvm::Registry`.
3. Rebuild and verify schema synchronization with `cmake --build build` and `./test.sh`.

## Releases

Releases are triggered automatically on push of a `v*` tag. The release workflow builds across Linux x64/arm64, macOS x64/arm64, and Windows x64, publishing platform binaries with `SHA256SUMS`, `LICENSE`, and `THIRD_PARTY_NOTICES`.
