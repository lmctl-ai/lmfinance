# Purpose and place

lmfinance is target 2 of the *scripted scientific libraries* family (`../../simulation/ROADMAP.md`,
procedure in `../../simulation/PROCEDURE.md`, template lmnum): QuantLib statically linked into one
executable and driven from lmscript `_dsl_` bytecode through an embedded schema.

- **Scripting only**: `--lmcode [--dry-run]`, `--version` (exactly `lmfinance X.Y.Z`), `--notices`,
  `--lmcode-schema [--json]`, `--lmcode-signatures`; exit codes 2 usage, 3 load/run error.
- **Namespaces** follow `simulation/brainstorm.txt`: `market`, `instrument`, `model`, `pricing`,
  `risk`, `calibration`, plus `lmf` for asserts and output. Each is a context object of the schema.
- **Design**: the host objects (`Curve`, `Option`, `Bond`, `Swap`, `Model`, `Engine`, `Result`,
  `HestonFit`) are plain value structs (`cpp/finance.hpp`). Every pricing call builds the QuantLib
  objects it needs and discards them, so there are no handle/observer lifetimes to manage and every
  call is a pure function of its arguments plus the evaluation date. Only yield curves keep QuantLib
  state (an immutable term structure).
- Out of scope for v0: schedules with calendars and business-day adjustment for bonds (bonds use the
  null calendar, unadjusted, 30/360), floating-rate bonds, caps/floors/swaptions, exotics, short-rate
  models, multi-curve discounting, inflation.
