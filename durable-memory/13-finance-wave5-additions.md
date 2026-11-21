# Finance wave 5 additions

Twenty-one methods, a yield guard and a set of Monte Carlo ceilings for six books that show their working (Black-Scholes step by
step, Monte Carlo pricing, caps/floors/swaptions, barrier and Asian options, Heston and the smile, duration hedging).
Same rules as [12-finance-track-additions.md](12-finance-track-additions.md): value structs, a schema entry generated
from `cpp/main.cpp` by `scripts/gen-schema.py`, code in `cpp/finance.cpp` (section "wave 5 additions"), a pinned test in
`tests/finance_wave5.lms` and one `expect_error` line per guard in `test.sh`. Every run error names the function and the
offending value. Nothing reads the system clock; every random draw comes from a seed the program gives.

## Conventions (read before using)

- **Time of an option-related `years`** is whole days on a 365-day year, as everywhere in the host: 0.5 is 183 days.
  `pricing.bs_d1/bs_d2`, `model.terminal_samples`, `model.sample_path` and `calibration.smile` use that time, so they agree
  with the engines. `pricing.black76` and `pricing.bachelier` take `expiry` as an exact number (no day grid).
- **Seeds** of the new functions are 1..2147483647 (the same on a 32-bit and a 64-bit build); 0 is refused.
- **Monte Carlo ceilings**: 4,000,000 paths; for an arithmetic Asian option 4,000,000 paths times fixings; for
  `monte_carlo_steps` 4,000,000 paths times steps. `pricing.monte_carlo` keeps its constructor range 16..100000000 and
  its memory-budget pre-check; the ceiling is checked when it prices, after that pre-check.
- **Units**: `risk.bond_dv01_yield` is currency for the bond's face per basis point of its own yield (prices from
  `bond_price_from_yield` are per 100 of face); `capfloor_*` and `swaption_annuity` are currency for the notional.

## The items

| # | Functions | Notes |
|---|---|---|
| 1 | `lmf.norm_cdf(x)`, `lmf.norm_pdf(x)`, `lmf.norm_inv(p)` | QuantLib `CumulativeNormalDistribution`, `NormalDistribution`, `InverseCumulativeNormal`. `norm_inv` outside (0, 1) is a run error. `norm_inv` is Acklam's approximation without refinement: 1.95996398612051 at 0.975 against the exact 1.959963984540054 (1.6e-9 off), so `norm_cdf(norm_inv(p))` returns p to about 1e-9, not to 1e-15. `risk.var_normal` uses the same quantile. |
| 2 | `pricing.bs_d1(option, model)`, `pricing.bs_d2(option, model)` | Formed as `AnalyticEuropeanEngine` forms them: expiry date `round(365 years)` days ahead, forward `S D_q / D_r`, `stdDev = sqrt(vol^2 t)`, `d1 = ln(F/K)/stdDev + stdDev/2`. `N(d1) e^(-qt)` is the engine's call delta (to 1e-14: the engine multiplies in another order). Standard case: 0.35 and 0.15; half a year (183 days): d1 0.247826165323959. Run error for a Heston model and for an American, barrier, Asian option or a forward. |
| 3 | `pricing.monte_carlo_plain(paths, seed) -> Engine` | The existing engine without the antithetic variate (`MakeMCEuropeanEngine`) and, for an arithmetic Asian option, without the antithetic and the control variate (`MakeMCDiscreteArithmeticAPEngine`). `Result.stderr()` works; `Engine.name()` is `monte_carlo_plain`. paths 2..4000000. Call S 100, K 100, 5%, 20%, 1 year, 10,000 paths, seed 42: 10.3470376223114, stderr 0.145036190939729 (antithetic engine: 10.431282614005202, 0.07384623684671558). Asian 12 fixings: 6.14897085224217, stderr 0.0855172204422917 (with both variates 6.156465446721795, 0.0020780833302327453). |
| 3b | path ceilings | `monte carlo: paths must be 1..4000000, got N`; `monte carlo: paths times fixings must be at most 4000000, got P x F = N`; the same with `steps`. Replaces the `std::bad_alloc` of a 10,000,000-path run under a 1024 MiB cap. |
| 4 | `model.terminal_samples(model, years, n, seed) -> FloatList`, `model.sample_path(model, years, steps, seed) -> FloatList` | Own code: `S exp((r - q - vol^2/2) t + vol sqrt(t) z)`, z = `InverseCumulativeNormal` of one `MersenneTwisterUniformRng` uniform per draw. n 1..10000, steps 1..1000 (steps + 1 prices, the first is the spot). The path sums the log-price and exponentiates once per point. Black-Scholes models only. `terminal_samples(m, T, n, seed)` draws exactly what `pricing.monte_carlo_plain(n, seed)` draws: the mean discounted payoff of the samples equals that engine's price to 4e-15 and the sample standard error its `stderr()` (pinned). Seed 42: 96.6600039801538, 121.636547890394, ...; path of 4 steps: 100, 97.5812092582015, ..., 116.870358090175. |
| 5 | `Option.path_payoff(prices) -> float` | Not discounted. Plain European option: payoff of the last price; forward: last price minus delivery price; barrier: touched when any point (the first included) is at or beyond the barrier (up `>=`, down `<=`), knocked out pays 0, knocked in pays the plain payoff of the last price; Asian: plain payoff of the arithmetic or geometric mean of the points after the first (the option's `fixings` count is not compared with the path length). American option: run error. Prices must be positive; at most 100,000 points. |
| 6 | `pricing.monte_carlo_steps(paths, seed, steps) -> Engine` | Barrier options only. QuantLib `MakeMCBarrierEngine<PseudoRandom>` with `withSteps(steps)` and `withBias()`: the barrier is looked at on the `steps` equally spaced simulated points and nowhere else (QuantLib's default pricer would correct for crossings between points with a Brownian-bridge probability and so estimate the continuous price; that correction is off). No antithetic variate, no Brownian-bridge path construction. steps 1..1000. A spot already at or beyond the barrier is a run error. Up-and-out call, barrier 120, 100,000 paths, seed 42, 10 steps: 1.93431948378999, stderr 0.0134091168209554; the continuously monitored closed form is 1.1760653996503727. |
| 7 | `pricing.black76(kind, forward, strike, vol, expiry, discount)`, `pricing.bachelier(kind, forward, strike, normal_vol, expiry, discount)` | kind `"call"` or `"put"`. QuantLib `blackFormula` / `bachelierBlackFormula` with `stdDev = vol sqrt(expiry)`, times `discount`. Black: forward, strike, vol, expiry, discount all positive; Bachelier: any forward and strike. `black76("call", 0.04, 0.04, 0.2, 1.0, 1.0)` = 0.00318622698216232 = F (2 N(0.1) - 1). |
| 8 | `pricing.capfloor_caplets(capfloor, curve, vol) -> FloatList`, `pricing.capfloor_forwards(capfloor, curve) -> FloatList`, `pricing.capfloor_swap_npv(capfloor, curve)`, `pricing.swaption_annuity(swaption, curve)` | Caplets: the Black engine's `optionletsPrice`; they sum to `capfloor_npv` (pinned to 1e-10 relative, measured 0). Forwards: the coupons' `adjustedFixing()`, the number the engine uses. Swap: `sum D(pay_i) N tau_i (F_i - K)` over the cap's own coupons (receive floating, pay the strike; the same value from a cap or a floor object), so cap - floor equals it (pinned to 1e-9 relative, measured 1.3e-15). Annuity: the Black swaption engine's `annuity` (\|fixed-leg BPS\| / 1 bp of the forward-starting swap, notional included, discounted to the evaluation date); payer = annuity x Black76 call on the forward swap rate (pinned to 1e-12 relative). 2025-01-02, flat 4%, 1,000,000, strike 4%, 5 years, vol 20%: 9 caplets, first 1061.08849207054, last 2791.11539655291, swap -609.029649175757; 1 year into 5 years: annuity 4265397.257471443. |
| 9 | `calibration.smile(model, strikes, years) -> FloatList`; `HestonFit.errors() -> FloatList`, `HestonFit.evaluations() -> int`, `HestonFit.reason() -> string` | Smile: the model's European call price (analytic engine) at each strike, inverted on a Black-Scholes model with the same spot, rate and dividend yield by `calibration.implied_vol`'s Brent search started at 20%; identical bits to doing that in the program. A Black-Scholes model returns its own vol exactly. 1..1000 strikes. `errors()`: per quote, in the order of the `quote()` calls, the model's implied volatility minus the quoted volatility (QuantLib `ImpliedVolError`): the residuals the fit minimises; `error()` is their root mean square. `evaluations()`: `functionEvaluation()` of the optimiser. `reason()`: the `EndCriteria::Type` as QuantLib prints it (`StationaryFunctionValue`, `StationaryPoint`, `StationaryFunctionAccuracy`, `ZeroGradientNorm`, `FunctionEpsilonTooSmall`; other outcomes are the existing "did not converge" error). All three before `fit()`: run error. |
| 10 | `risk.bond_dv01_yield(bond, yield)`; yield guard | Modified duration x dirty value x 0.0001, the bond's own compounding (its coupon frequency; annual for a zero bond), currency for the bond's FACE: 0.7721734929184812 for face 1000, 5% annual, 10 years at 5%. A central bump of 1 bp on the yield gives 0.7721736303739846: they agree to 2e-7 relative (the difference is the third-order term). Guard on `pricing.bond_price_from_yield`, `bond_duration`, `bond_macaulay_duration`, `bond_convexity` and `risk.bond_dv01_yield`: the yield must be above -100% per coupon period, that is above -m for m periods a year (it was a number without meaning before: -20.02 for a duration at -150%). |

## Run errors added (exact text)

- `lmf.norm_inv: probability must be inside (0, 1), got P`
- `pricing.bs_d1: needs a Black-Scholes model, not a Heston model`; `pricing.bs_d1: needs a plain European option (this one is American)` (also `is a barrier option`, `is an Asian option`, `is a forward`); the same for `bs_d2`
- `monte carlo: paths must be 1..4000000, got N`; `monte carlo: the standard error needs at least 2 paths, got 1`
- `monte carlo: paths times fixings must be at most 4000000, got P x F = N`; `monte carlo: paths times steps must be at most 4000000, got P x S = N`
- `pricing.monte_carlo_plain: seed must be 1..2147483647 (0 would mean a clock-based seed), got N` (also `monte_carlo_steps`, `model.terminal_samples`, `model.sample_path`)
- `pricing.monte_carlo_steps: steps must be 1..1000, got N`; `pricing.monte_carlo_steps prices barrier options; use pricing.monte_carlo or pricing.monte_carlo_plain for this option`
- `barrier touched: the spot S is already at or beyond the TYPE barrier H` (stepped engine)
- `model.terminal_samples: n must be 1..10000, got N`; `model.sample_path: steps must be 1..1000, got N`; `...: needs a Black-Scholes model, not a Heston model`; `...: years must be above 0 and at most 100, got Y`; `...: Y years is shorter than one day (1/365 year)`
- `Option.path_payoff: the path is empty`; `... prices must be positive, got X at index I`; `... the path has N points, at most 100000`; `... an American option's payoff depends on when it is exercised; use Option.payoff(spot) at the exercise point`; `... an Asian payoff needs the price today and at least one fixing, got 1 point`
- `pricing.black76: kind must be call or put, got 'K'`; `pricing.black76: forward must be positive, got F (use pricing.bachelier for a negative forward)`; `...: strike / vol / expiry / discount must be positive, got X`; the same for `pricing.bachelier` (vol, expiry, discount)
- `calibration.smile: needs 1..1000 strikes, got N`; `calibration.smile: strikes must be positive, got K at index I`; `calibration.smile: no implied volatility at strike K (reason)`
- `HestonFit.errors: call fit() first` (also `evaluations`, `reason`)
- `pricing.bond_duration: yield must be above -100% per coupon period (above -100% a year for 1 period a year), got Y` (the function named is the one called)

## Existing behaviour that changed

Only the guards:

1. Monte Carlo ceilings. `pricing.monte_carlo(paths, seed)` with more than 4,000,000 paths now stops when it prices
   (it ran before: 10,000,000 paths took 7 s natively and ended in `std::bad_alloc` under a 1024 MiB cap). An arithmetic
   Asian option with paths times fixings above 4,000,000 stops (1,000,000 paths x 12 fixings ran in 8 s before). When the
   memory budget refuses the run, its message still comes first (100,000,000 paths under `LMFINANCE_MAX_MEMORY_MB=64`:
   unchanged text, pinned by `test.sh`).
2. Three gallery examples ran above the ceiling and were shortened (their own commit, so it can be judged apart):
   `asian_options.lms` 40,000 paths with up to 128 fixings (5,120,000) now uses 30,000 paths; `mc_convergence.lms` went up
   to 8,192,000 paths and now stops at 2,048,000 (7 points instead of 8, "six of seven inside 3 standard errors");
   `engine_convergence.lms` went up to 16,384,000 and now stops at 1,024,000 (6 points instead of 8). The rows they still
   write are byte-identical to the old ones. `docs/images/{asian_options,mc_convergence,engine_convergence}.png` were
   NOT re-rendered here (no lmplot on the host): they show the old runs until `examples/render-all.sh` runs.
3. Yield guard: a yield at or below -100% per coupon period stops in the four bond yield functions.

Everything else is unchanged. Measured on the native aarch64 binaries of 68288870 and of this wave, through lmscript:
of the 140 finance-track probe programs that were run (one, 100,000,000 paths, was left out), 130 give byte-identical
output; 5 run into ceiling 1 (6, 8 and 10 million paths; Asian 100,000 x 52 and 1,000,000 x 12), 1 into the yield guard,
and 4 are compile-error probes for calls that now exist or whose "methods:" list grew. The finance blocks of 25 other
gallery examples write byte-identical output and data files (`mc_var`, which needs lmnum, and `option_decay` were not run).

## Deviations from the proposal and honest notes

- **The ceiling is one number on every platform**, although only a memory-capped build needs it: a lesson's stop message
  has to be the same in the browser and on the native witness. The price is the three shortened gallery examples above. If
  native runs above 4,000,000 paths matter more, the number is `mc_max_paths` in `cpp/finance.hpp`.
- **Ceiling placement.** The brief's wording is one message for every engine. The constructors of the new engines check
  it at once; `pricing.monte_carlo` checks it at pricing time and keeps its own constructor message
  (`pricing.monte_carlo: paths must be 16..100000000`), because `test.sh` pins the memory-budget message of a
  100,000,000-path engine and existing programs build engines with that range.
- **One path.** The message says 1..4000000, but QuantLib cannot form a standard error from one sample, so the plain and
  stepped engines ask for at least 2 paths with their own message. The antithetic engine keeps its minimum of 16.
- **Old stop messages kept word for word**, although two are now incomplete: `barrier options are priced with the
  analytic engine only` (also `monte_carlo_steps`) and `arithmetic Asian options are priced with the monte_carlo engine
  (geometric control variate)` (also `monte_carlo_plain`, without the control variate). Recorded outputs depend on the text.
- **`monte_carlo_steps` is barrier-only.** It was small (one branch). It monitors discretely, so it does NOT converge to
  the analytic price at a fixed step count: with 10 steps the up-and-out call above is 1.93 against 1.18. The
  Broadie-Glasserman-Kou shifted barrier (`H exp(0.5826 vol sqrt(T/steps))`) is a rough guide only: at 10 steps it gives
  2.056 where the simulation gives 1.899 +- 0.007 (400,000 paths) and the in-page simulation 1.916 +- 0.021; at 50 steps
  1.540 against 1.499 +- 0.013. The test compares the engine with the program's own loop over `sample_path` and
  `path_payoff` (independent code and draws) instead.
- **`path_payoff` and the barrier's first point.** `path_payoff` counts the first point; QuantLib's biased pricer starts
  at the second. They can only differ when the spot is already beyond the barrier, which the stepped engine refuses.
- **Swaption time.** The Black swaption formula's time is the exercise date on Act/365: 1.0 for a 1-year expiry from
  2025-01-02, 181/365 for a 6-month expiry (pinned). It is not exposed as a function; lessons should use whole years
  from 2025-01-02 or state the day count.
- **Cap periods.** `MakeCapFloor` leaves out the first period, so a 5-year cap on 6M has 9 caplets and
  `capfloor_swap_npv` is the swap over those 9 periods (Act/360, TARGET), not `instrument.swap` (annual 30/360 fixed leg
  from today). The second caplet by hand: `N (181/360) black76("call", F_2, K, vol, 1.0, D(550/365))`, pinned.
- **Smile on deep in-the-money calls.** The call is used at every strike as specified; far in the money its price is
  nearly `S - K D` and the inversion is ill-conditioned (it still returns a number: strike 1 with spot 100 runs).
- **HestonFit.evaluations/reason are platform facts.** Native aarch64 gives 32 evaluations and
  `StationaryFunctionValue` for the test's 15 quotes; the calibration iterates differ between WebAssembly and native
  (see the probe report), so the count may differ there. The test pins a range and the set of accepted reasons, not the
  count. A lesson should not print the count unless it was recorded on the runtime that shows it.
- **Not built or measured here: WebAssembly.** Expected, not measured: closed forms (`norm_cdf`, `norm_pdf`, d1/d2,
  Black 76, Bachelier, caplets on a flat curve, yield DV01, path payoffs) agree with native to the last digit or two;
  `norm_inv`, `terminal_samples`, `sample_path` and every Monte Carlo price go through `log`/`exp` of the C library and
  may differ in the last digits (the probe report measured 1.6e-13 for the quantile); the generator and the draws are
  integer arithmetic and identical. Lessons should return rounded values for these.
- **Version.** `CMakeLists.txt` still says 0.1.4 (`test.sh` pins it); the release that carries this wave has to bump both.
- Own code: items 2 (arithmetic), 4, 5, the cap's swap in 8, 9 (smile), 10 (the product); QuantLib classes as named:
  1, 3, 6, 7, the rest of 8, the fit report in 9. No code of any other tool was used.

## Examples

```
let m = model.black_scholes(100.0, 0.05, 0.0, 0.2);
let call = instrument.european_call(100.0, 1.0);
lmf.print(str(lmf.round(lmf.norm_cdf(pricing.bs_d1(call, m)), 4)) + " " + str(lmf.round(lmf.norm_cdf(pricing.bs_d2(call, m)), 4)));   # 0.6368 0.5596
let st = model.terminal_samples(m, 1.0, 10000, 42);          # average call.payoff(st.at(i)) * e^(-rT) yourself: 10.347
lmf.print(str(lmf.round(pricing.price(call, m, pricing.monte_carlo_plain(10000, 42)).price(), 4)));   # 10.347
let uo = instrument.barrier_call(100.0, 1.0, 120.0, "up-out");
lmf.print(str(uo.path_payoff(model.sample_path(m, 1.0, 10, 42))));
lmf.print(str(lmf.round(pricing.price(uo, m, pricing.monte_carlo_steps(100000, 42, 10)).price(), 2)));   # 1.93
market.set_date("2025-01-02");
let fc = market.flat_curve(0.04);
let cap = instrument.cap(1000000.0, 0.04, 5.0);
lmf.print(lmf.money(pricing.capfloor_swap_npv(cap, fc)));    # -609.03 = cap - floor
lmf.print(lmf.money(risk.bond_dv01_yield(instrument.bond(1000.0, 0.05, 10.0, 1), 0.05) * 100.0));   # 77.22 per 100 bp
```

The pinned values are in `tests/finance_wave5.lms`; `test.sh` runs it with the other programs.

## Ceiling scope (lead's decision, 2026-11-21)

The 4,000,000 ceilings bind `pricing.monte_carlo_plain` and `pricing.monte_carlo_steps` on every platform. The older
`pricing.monte_carlo` is unchanged natively (the gallery examples run it with up to 16,384,000 paths; they are not
shortened) and gets the same ceilings only in a 32-bit build (WebAssembly), where an unbounded run ended in
`std::bad_alloc`. Statements above that say `pricing.monte_carlo` stops above 4,000,000 paths hold for the
WebAssembly build only; the two native tests for them were removed from `test.sh`.
