# Build, run, test

Measured 2026-10-23, x86_64 Linux (WSL2), 4 cores, GCC, Ninja.

- Clean Release build (717 QuantLib translation units at -O1 plus the host): about 3 min 20 s.
  Linux uses dynamic glibc >= 2.35, static QuantLib and static libstdc++/libgcc. Boost headers 184 MB in
  `third_party/` (not in the binary).
- `./test.sh` (18 checks): sdk pin; deps pin; build; `--version`; schema equals the registrations;
  embedded schema byte-identical; host registry equals compiler signatures; notices; Linux ELF dependency allowlist and glibc 2.35 ABI audit;
  14 programs (9 under `tests/`, 5 under `examples/`) compile and run and pass their own asserts;
  5 result files byte-identical on rerun; 28 error snippets exit 3 with a message naming the
  problem; all 28 gallery examples when `lmplot` is on `PATH` or `LMFINANCE_TEST_NETWORK=1`;
  usage/load exit codes. With a warm build the whole script runs in about 7 s (Monte Carlo test 0.6 s).
- `test.sh` also checks the memory budget (`09-resource-limits.md`): a runaway loop and 1e8 Monte
  Carlo paths at `LMFINANCE_MAX_MEMORY_MB=64`, a bad value exits 2, and on Linux the default
  4096 MiB under `ulimit -v 6291456` (about 23 s of the run).
- Run a program by hand: `lmscript examples/bond_curve.lms` (compiles, binds to `lmfinance` on PATH).

The local v0.1.3 Linux x64 binary is 5,405,992 bytes stripped, needs only libm.so.6, libc.so.6 and ld-linux-x86-64.so.2, and has maximum GLIBC_2.34 (within the supported glibc >= 2.35 baseline). Three README samples also pass in fresh directories against the built finance binary and published lmplot v0.2.1: option_greeks, barrier_options and var_tail.
