# Resource limits

Why: host objects live in arenas until exit, so a DSL loop that keeps creating them grows without
bound. On 2026-10-23 a runaway `lmnum` process reached 16 GB RSS and filled the 24 GB WSL machine;
lmfinance had the same exposure.

- **Total memory budget**, default 4096 MiB, set with `LMFINANCE_MAX_MEMORY_MB` (positive whole MiB;
  anything else exits 2 with the usage text). Shown in the usage message.
- `cpp/memory_budget.{hpp,cpp}` (identical copy in every lm* host) replaces the global
  `operator new`/`delete` and counts block sizes from the C allocator (`malloc_usable_size`,
  `malloc_size`, `_msize`). QuantLib and Boost (headers) allocate only through `new` and standard
  containers, so everything is counted without `charge()`: the arenas (curves, builders, options,
  models, engines, results, fits; at most 4,000,000 each), curve builder quote lists, Heston quote
  lists, bootstrapped curves and their observer links, finite-difference grids, binomial lattices,
  Monte Carlo statistics, `save_*` output streams and DSL strings in the VM.
- Exceeding it throws `membudget::Exceeded` (a `std::bad_alloc` with a fixed-buffer message) from
  wherever QuantLib allocates; it unwinds through C++ only (QuantLib has no C callbacks), the
  method wrapper turns it into the run error, and `main` catches `std::exception`
  around load and run for the VM. The first failure prints `lmfinance: memory budget exceeded: ...
  (set LMFINANCE_MAX_MEMORY_MB ...)` on stderr; exit 3. 64 MiB of headroom is granted once so the
  error path can still allocate.
- **Pre-check**: QuantLib's Monte Carlo statistics keep every sample (16-byte pairs), so
  `pricing.monte_carlo(1e8, ...)` needs about 1.6 GB at the end and ~3 GiB of address space while
  the vector grows. `price_option` requires `paths * 1.5 * 16` bytes (the least that run must hold)
  before simulating, so a too-large run fails at once instead of after a minute. Measured: 1e8 paths
  with the default budget run (68 s, 2.1 GB peak RSS); at 2048 MiB they are refused immediately.
- Not covered: nothing in this host allocates outside `operator new`. Allocator chunk headers are
  not in the count, which is why RSS can exceed the tracked total by ~10% for many small objects.
- No `setrlimit`: RLIMIT_AS is not portable to macOS static builds and would make the host
  itself crash instead of reporting.
- Measured (x86_64 Linux): the test runaway (a loop of `c.bumped(0.0001)`, ~1.3 KB of arena per
  iteration, the biggest host object a loop makes quickly; bootstrapped curves are 44 KB but 30x
  slower) stops at 64 MiB in 0.25 s, and at the default 4096 MiB after 23 s with 4.6 GB peak RSS
  (small-block allocator overhead). `test.sh` runs both; the default-budget run only on Linux,
  under `ulimit -v 6291456`.
