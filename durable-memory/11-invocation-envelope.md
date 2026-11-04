# 11 — Invocation envelope (2026-11-04 port)

`main()` answers `--lmcode FILE...` with the lmcode-cpp `InvocationResults` one-envelope frame
(`LMCODE_RETURN_BEGIN` / `END` around the JSON result), replacing the per-program stdout
`report_ok()` / `report_error()` lines. Shape after the identical lmphysics/lmweather ports:

- CLI usage mistakes print the usage text on stderr, then a `failure_frame("cli", ...)` frame, and
  exit 2 (bad flags, missing file, bad `LMFINANCE_MAX_MEMORY_MB`, duplicate/dry contradiction).
- `results.registrations()` lists the files; every file is loaded and verified FIRST, so a broken
  later file cannot leave earlier tables behind.
- Each program: `results.select(index, reg, contract)`, run only when `!results.dry()`,
  `results.complete(index)`; on error `results.fail(index, phase, e)` and stop. Phases: `load`,
  `run`, `app` when the exception code is `APP_FAIL`.
- Dry mode is invocation-wide: `run_lmcode` under `lmscript --dry-run` supplies `dry: true` in
  `LMCODE_INVOCATION_FILE`; the standalone `--dry-run` flag requests the same mode when no metadata
  is present. A dry invocation loads every program but runs none (valuations write tables; there is
  no effect-free mode).
- A 1 MiB reserve is released before recording any failure so a budget-exhausted program can still
  have its frame written.
- Context objects unchanged: market, instrument, model, pricing, risk, calibration, lmf are all set
  on the one host instance as before.
- Versions: lmfinance 0.1.4, vendored SDK at lmcode-cpp origin/main (invocation.hpp,
  result_contract.hpp added). `scripts/select-compiler.sh` is the shared capability probe (checks a
  block with `run_lmcode` against a plain schema; it does not install anything).
