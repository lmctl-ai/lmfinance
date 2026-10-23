# Build, run, test

Measured 2026-10-23, x86_64 Linux (WSL2), 4 cores, GCC, Ninja.

- Clean Release build (717 QuantLib translation units at -O1 plus the host): about 3 min 20 s.
  Binary 5.7 MB (stripped, `-static`), `ldd`: "not a dynamic executable". Boost headers 184 MB in
  `third_party/` (not in the binary).
- `./test.sh` (15 checks): sdk pin; deps pin; build; `--version`; schema equals the registrations;
  embedded schema byte-identical; host registry equals compiler signatures; notices; static linking;
  12 programs (7 under `tests/`, 5 under `examples/`) compile and run and pass their own asserts;
  6 result files byte-identical on rerun; 16 error snippets exit 3 with a message naming the
  problem; the two lmfinance+lmplot pipelines when `lmplot` is on `PATH` or `LMFINANCE_TEST_NETWORK=1`;
  usage/load exit codes. With a warm build the whole script runs in about 7 s (Monte Carlo test 0.6 s).
- `test.sh` also checks the memory budget (`09-resource-limits.md`): a runaway loop and 1e8 Monte
  Carlo paths at `LMFINANCE_MAX_MEMORY_MB=64`, a bad value exits 2, and on Linux the default
  4096 MiB under `ulimit -v 6291456` (about 23 s of the run).
- Run a program by hand: `lmscript examples/bond_curve.lms` (compiles, binds to `lmfinance` on PATH).
