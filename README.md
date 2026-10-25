# lmfinance

**QuantLib for lmscript: yield curves, bonds, swaps, caps and swaptions, vanilla and exotic options, Greeks, risk and calibration in one executable.**

[![CI](https://github.com/lmctl-ai/lmfinance/actions/workflows/ci.yml/badge.svg)](https://github.com/lmctl-ai/lmfinance/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/lmctl-ai/lmfinance?label=release)](https://github.com/lmctl-ai/lmfinance/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

### Options

| ![option_decay](docs/images/option_decay.gif) | ![option_greeks](docs/images/option_greeks.png) |
| --- | --- |
| A call melting onto its payoff as expiry nears (needs ffmpeg).<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/option_decay.lms` | Call price and delta against the spot.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/option_greeks.lms` |

| ![greeks_surface](docs/images/greeks_surface.png) | ![option_strategies](docs/images/option_strategies.png) |
| --- | --- |
| Delta, gamma, vega and theta as surfaces over spot and time to expiry.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/greeks_surface.lms` | Straddle, bull spread, butterfly: value today against the payoff.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/option_strategies.lms` |

| ![american_put](docs/images/american_put.png) | ![exercise_boundary](docs/images/exercise_boundary.png) |
| --- | --- |
| American against European put: the early-exercise premium.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/american_put.lms` | Where to exercise an American put: the critical spot against time, above the perpetual limit.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/exercise_boundary.lms` |

| ![gamma_heatmap](docs/images/gamma_heatmap.png) | ![tree_convergence](docs/images/tree_convergence.png) |
| --- | --- |
| Gamma: the at-the-money spike that sharpens toward expiry.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/gamma_heatmap.lms` | Binomial trees against Black-Scholes: odd/even oscillation, Leisen-Reimer converging like 1/n^2.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/tree_convergence.lms` |

| ![barrier_options](docs/images/barrier_options.png) | ![asian_options](docs/images/asian_options.png) |
| --- | --- |
| Up-and-out calls: in + out = vanilla, and a knock-out call whose vega turns negative.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/barrier_options.lms` | Asian options: averaging makes them cheaper, geometric in closed form, arithmetic by Monte Carlo.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/asian_options.lms` |

### Volatility

| ![vol_smile](docs/images/vol_smile.png) | ![vol_surface](docs/images/vol_surface.png) |
| --- | --- |
| Heston implied-volatility smile for three correlations.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/vol_smile.lms` | Heston implied-volatility surface: skew and term structure.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/vol_surface.lms` |

| ![heston_calibration](docs/images/heston_calibration.png) | |
| --- | --- |
| Heston calibrated to a 4 x 9 vol surface from a wrong guess: fitted smiles on the quotes, residuals below 1e-4.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/heston_calibration.lms` | |

### Rates and bonds

| ![yield_curve](docs/images/yield_curve.png) | ![curve_shapes](docs/images/curve_shapes.png) |
| --- | --- |
| A bootstrapped curve: zero and forward rates.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/yield_curve.lms` | Normal, flat and inverted curves: where the forwards sit and what the same bond and swap are worth on each.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/curve_shapes.lms` |

| ![bond_convexity](docs/images/bond_convexity.png) | ![bond_ladder](docs/images/bond_ladder.png) |
| --- | --- |
| Bond price against yield, with the duration tangent.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/bond_convexity.lms` | Price, modified duration and convexity across the maturity ladder for three coupons; par and zero-bond answers asserted.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/bond_ladder.lms` |

| ![bond_balance](docs/images/bond_balance.png) | ![swap_cashflows](docs/images/swap_cashflows.png) |
| --- | --- |
| Duration is the balance point of the discounted cash flows (lmdraw).<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/bond_balance.lms` | A payer swap flow by flow: fixed coupons down, floating coupons from the forward rates up, each with its PV (lmdraw).<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/swap_cashflows.lms` |

| ![rate_scenarios](docs/images/rate_scenarios.png) | ![dv01_ladder](docs/images/dv01_ladder.png) |
| --- | --- |
| Bond and swap P/L under parallel curve shifts.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/rate_scenarios.lms` | Where the rate risk sits: DV01 of zero bonds and swaps.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/dv01_ladder.lms` |

| ![caps_floors](docs/images/caps_floors.png) | ![swaption_grid](docs/images/swaption_grid.png) |
| --- | --- |
| A 5-year cap and floor against the strike: equal at the ATM rate, cap - floor is a swap.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/caps_floors.lms` | ATM payer swaptions over expiry x tenor, and the 1Y x 5Y swaption against the strike.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/swaption_grid.lms` |

### Numerics and risk

| ![engine_convergence](docs/images/engine_convergence.png) | ![mc_convergence](docs/images/mc_convergence.png) |
| --- | --- |
| Tree, finite-difference and Monte Carlo errors against the closed form.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/engine_convergence.lms` | Seeded Monte Carlo inside its error bars.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/mc_convergence.lms` |

| ![var_tail](docs/images/var_tail.png) | ![mc_var](docs/images/mc_var.png) |
| --- | --- |
| 99% VaR and expected shortfall of a position.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/var_tail.lms` | Monte Carlo VaR by full revaluation: lmnum normals, lmfinance prices, `risk.series` quantiles.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/mc_var.lms` |

| ![portfolio_waterfall](docs/images/portfolio_waterfall.png) | |
| --- | --- |
| P/L attribution of an option book: delta, gamma, vega, rho, theta and the unexplained remainder.<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/portfolio_waterfall.lms` | |

Each command runs the example from any empty directory: lmfinance computes, [lmplot](https://github.com/lmctl-ai/lmplot) or [lmdraw](https://github.com/lmctl-ai/lmdraw) draws, and the picture lands in `images/`. `examples/render-all.sh` regenerates `docs/images/` from a checkout.

**Quick start:** `npm i -g @lmctl-ai/lmctl`, then paste any `lmctl autorun` line above.
Linux downloads require **dynamic glibc >= 2.35** (Ubuntu 22.04 or newer).

It is a scripting host,
not a command-line tool: write an [lmscript](https://lmctl.com/tools/lmscript/skill.html) `_dsl_`
block, compile it, run it with `lmfinance --dsl`. No Python or Boost shared libraries are needed. Linux uses system glibc;
QuantLib and the GCC C++ runtimes are static.

```
# bs.lms: Black-Scholes call S=100 K=100 r=5% vol=20% T=1 is worth 10.4506
p = _lmcode_ cli="lmfinance" install="github:lmctl-ai/lmfinance@latest";
let m = model.black_scholes(100.0, 0.05, 0.0, 0.2);
let call = instrument.european_call(100.0, 1.0);
let r = pricing.price(call, m, pricing.analytic());
lmf.print("price " + str(r.price()) + " delta " + str(r.delta()));
lmf.assert_close(r.price(), 10.4506, 0.00005);
end _lmcode_
savedsl(p, "bs.dslb");
system ${dsl_cli(p)} --dsl bs.dslb;
```

`lmscript bs.lms` finds `lmfinance` on `PATH` or downloads the release binary for your platform on
first use, compiles against the schema built into it, and runs the program.

## What it does

Six namespaces, each a context object of the schema; objects are built by chained methods
(the DSL has no lists or closures). Rates are decimals (`0.05`), maturities are years, dates are
`YYYY-MM-DD` strings. The evaluation date defaults to 2024-01-02 (never the system clock) and is set
with `market.set_date`.

| namespace | functions |
| --- | --- |
| `market` | `set_date` `date` `flat_curve(r)` `zero_curve()` `bootstrap_curve()` → `CurveBuilder` `.zero(years, rate)` `.deposit("3M", rate)` `.swap("5Y", rate)` `.build()` → `Curve` `.discount .zero .forward .bumped .save_csv` |
| `instrument` | `european_call/put(K, T)` `american_call/put(K, T)` `barrier_call/put(K, T, barrier, "up-out"\|"up-in"\|"down-out"\|"down-in")` `asian_call/put(K, T, "geometric"\|"arithmetic", fixings)` → `Option`; `bond(face, coupon, years, freq)` `zero_bond(face, years)` → `Bond`; `swap(notional, fixed_rate, years)` → `Swap` `.receiver .payer .spread`; `cap/floor(notional, strike, years)` → `CapFloor`; `swaption(swap, expiry)` → `Swaption` |
| `model` | `black_scholes(S, r, q, vol)` `heston(S, r, q, v0, kappa, theta, sigma, rho)` → `Model` `.param .with_spot .with_rate .with_vol` |
| `pricing` | engines `analytic()` `binomial(n)` `binomial_tree("crr"\|"jr"\|"tian"\|"lr", n)` `finite_diff(tsteps, grid)` `monte_carlo(paths, seed)` `baw()`; `price(option, model, engine)` → `Result` `.price .stderr .delta .gamma .vega .theta .rho`; bonds: `bond_clean_price bond_npv bond_accrued bond_price_from_yield bond_yield bond_duration bond_macaulay_duration bond_convexity`; swaps: `swap_npv swap_par_rate swap_annuity`; Black: `capfloor_npv(cf, curve, vol) capfloor_implied_vol capfloor_atm_rate swaption_npv(sw, curve, vol) swaption_implied_vol swaption_forward_rate` |
| `risk` | bumped `delta gamma vega rho theta` for any engine; `bond_dv01` `swap_dv01`; `var_normal` `es_normal`; `series(path)` → `Series` `.size .get .mean .std .min .max .quantile(p) .var(conf) .es(conf)` (empirical VaR/ES of a P/L or return file); `save_profile` (price, delta, gamma over a spot range, CSV) |
| `calibration` | `implied_vol(option, model, engine, price)`; `heston(S, r, q)` → `HestonFit` `.quote(K, T, vol)` `.guess(...)` `.fit()` → `Model` `.error()` |
| `lmf` | `assert_close` `check` `print` `save_text` `version` |

Conventions and details: [durable-memory/03-schema-v0.md](durable-memory/03-schema-v0.md).

Combine it with other hosts in one script: [examples/gallery/](examples/gallery) bootstraps curves, prices and profiles options with lmfinance and draws them with [lmplot](https://github.com/lmctl-ai/lmplot)
(two `_dsl_` blocks, two hosts).

## Examples

| Program | Shows | Run it |
| --- | --- | --- |
| [option_pricing.lms](examples/option_pricing.lms) | one call and one American put through every engine, binomial error against the closed form | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/option_pricing.lms` |
| [bond_curve.lms](examples/bond_curve.lms) | curve bootstrapped from deposits and swaps reprices its inputs; bond price, yield, duration, DV01 | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/bond_curve.lms` |
| [greeks.lms](examples/greeks.lms) | analytic, tree and Monte Carlo Greeks agree | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/greeks.lms` |
| [heston_smile.lms](examples/heston_smile.lms) | a Heston smile and a calibration that recovers its five parameters | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/heston_smile.lms` |
| [portfolio_risk.lms](examples/portfolio_risk.lms) | DV01s, normal VaR and expected shortfall of a small book | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/portfolio_risk.lms` |
| [gallery/option_decay.lms](examples/gallery/option_decay.lms) | Call value melting onto its payoff, animated | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/option_decay.lms` |
| [gallery/yield_curve.lms](examples/gallery/yield_curve.lms) | Bootstrapped yield curve: zero and forward rates | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/yield_curve.lms` |
| [gallery/option_greeks.lms](examples/gallery/option_greeks.lms) | Call price and delta against spot | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/option_greeks.lms` |
| [gallery/american_put.lms](examples/gallery/american_put.lms) | American vs European put: early-exercise premium | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/american_put.lms` |
| [gallery/bond_convexity.lms](examples/gallery/bond_convexity.lms) | Bond price against yield with the duration tangent | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/bond_convexity.lms` |
| [gallery/rate_scenarios.lms](examples/gallery/rate_scenarios.lms) | Bond and swap P/L under parallel curve shifts | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/rate_scenarios.lms` |
| [gallery/vol_smile.lms](examples/gallery/vol_smile.lms) | Heston implied-volatility smile for three correlations | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/vol_smile.lms` |
| [gallery/engine_convergence.lms](examples/gallery/engine_convergence.lms) | Tree, finite-difference and Monte Carlo error | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/engine_convergence.lms` |
| [gallery/mc_convergence.lms](examples/gallery/mc_convergence.lms) | Seeded Monte Carlo inside its error bars | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/mc_convergence.lms` |
| [gallery/option_strategies.lms](examples/gallery/option_strategies.lms) | Straddle, bull spread and butterfly: value today against payoff; put-call parity asserted | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/option_strategies.lms` |
| [gallery/gamma_heatmap.lms](examples/gallery/gamma_heatmap.lms) | Gamma over spot and time to expiry, checked against the Black-Scholes formula | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/gamma_heatmap.lms` |
| [gallery/vol_surface.lms](examples/gallery/vol_surface.lms) | Heston implied-volatility surface: skew and term structure | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/vol_surface.lms` |
| [gallery/bond_balance.lms](examples/gallery/bond_balance.lms) | Duration as the balance point of discounted cash flows (lmdraw) | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/bond_balance.lms` |
| [gallery/dv01_ladder.lms](examples/gallery/dv01_ladder.lms) | DV01 of zero bonds and swaps against P T and the annuity | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/dv01_ladder.lms` |
| [gallery/var_tail.lms](examples/gallery/var_tail.lms) | 99% VaR and expected shortfall of a position | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/var_tail.lms` |
| [gallery/curve_shapes.lms](examples/gallery/curve_shapes.lms) | three `zero_curve` shapes: forwards above a rising curve and below an inverted one, flat forward = zero to 1e-12, bond and swap repriced on each | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/curve_shapes.lms` |
| [gallery/bond_ladder.lms](examples/gallery/bond_ladder.lms) | 1..30 years x 2/5/8% coupons at a 5% yield: par bond at 100, zero-bond Macaulay = T and modified = T/1.05, 10-year Macaulay 8.1078 | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/bond_ladder.lms` |
| [gallery/greeks_surface.lms](examples/gallery/greeks_surface.lms) | 41 x 40 grid of analytic Greeks; ATM delta 0.6368, gamma integrates to delta(140) - delta(60), theta fastest ATM near expiry | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/greeks_surface.lms` |
| [gallery/tree_convergence.lms](examples/gallery/tree_convergence.lms) | crr/jr/tian/lr for 5..400 steps; CRR brackets the closed form on odd/even steps, Leisen-Reimer within 1e-5 at 201 steps | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/tree_convergence.lms` |
| [gallery/exercise_boundary.lms](examples/gallery/exercise_boundary.lms) | bisection on CRR 400 for the critical spot S*(T): below K, falling with T, above the perpetual limit 71.43 | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/exercise_boundary.lms` |
| [gallery/heston_calibration.lms](examples/gallery/heston_calibration.lms) | 36 quotes from a known Heston model, fit from a wrong guess: rms error < 1e-4, rho and v0 recovered; non-convergence is an error since 0.1.2 | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/heston_calibration.lms` |
| [gallery/barrier_options.lms](examples/gallery/barrier_options.lms) | in + out = vanilla to 1e-6 for up and down barriers; the up-and-out call loses half its peak value at high vol | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/barrier_options.lms` |
| [gallery/asian_options.lms](examples/gallery/asian_options.lms) | one fixing = vanilla; geometric closed form decreasing in fixings; arithmetic MC with control variate, stderr < 0.01 | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/asian_options.lms` |
| [gallery/caps_floors.lms](examples/gallery/caps_floors.lms) | cap = floor at `capfloor_atm_rate`, cap - floor linear in the strike, `capfloor_implied_vol` returns 30% at every strike | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/caps_floors.lms` |
| [gallery/swaption_grid.lms](examples/gallery/swaption_grid.lms) | ATM payer = receiver on a 4 x 4 grid, payer - receiver = annuity (fwd - K), implied vol round trip | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/swaption_grid.lms` |
| [gallery/mc_var.lms](examples/gallery/mc_var.lms) | lmnum draws correlated normals, lmfinance reprices a book in 4000 scenarios, `risk.series` gives VaR and ES; compared with `var_normal` | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/mc_var.lms` |
| [gallery/portfolio_waterfall.lms](examples/gallery/portfolio_waterfall.lms) | Greek attribution of the P/L after spot +6, vol +3, rates +25bp, 10 days: the unexplained remainder is under 6% of the total | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/portfolio_waterfall.lms` |
| [gallery/swap_cashflows.lms](examples/gallery/swap_cashflows.lms) | lmdraw cash-flow diagram; the two legs rebuilt from `Curve.forward` and `discount` match `swap_npv` to 0.1% of notional | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/gallery/swap_cashflows.lms` |

`tests/` holds the checks against textbook values (Black-Scholes 10.4506 and 5.5735, Hull's
example, the American put 6.0903, bond annuity formulas, put-call parity, Heston → Black-Scholes
limit, bootstrap round trips).

## Build

C++17 compiler, CMake 3.24, git, curl; nothing else.

```sh
scripts/sync-deps.sh            # QuantLib at the commit and Boost headers at the sha256 in deps.lock
cmake -S . -B build && cmake --build build     # Release, -O1 for QuantLib, about 3.5 minutes on 4 cores
./test.sh                       # needs lmscript on PATH
```

Linux uses dynamic glibc >= 2.35, with QuantLib and the GCC C++ runtimes static.
macOS links only system libraries; Windows links the MinGW runtime statically. `lmfinance --notices` prints the license texts of everything inside.

`lmfinance --version` remains exactly `lmfinance X.Y.Z` for installer compatibility.
The Linux runtime baseline applies to both Linux release assets; no relink-object
archive or third-party shared libraries are required.

## Resource limits

A program may use at most 4096 MiB in total (curves, objects, QuantLib engines and Monte Carlo samples,
everything the host keeps); beyond that it stops with a run error (exit 3) naming the limit, instead
of exhausting the machine. Set `LMFINANCE_MAX_MEMORY_MB` to change it, e.g.
`LMFINANCE_MAX_MEMORY_MB=16384 lmscript big.lms`.

## License

MIT ([LICENSE](LICENSE)). QuantLib is a modified BSD license and Boost (headers only) is BSL-1.0;
see [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).
