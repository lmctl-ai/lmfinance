# Finance-track additions

Twenty small additions that let a textbook finance course (per-period rates, whole-number periods, cents, explicit
binomial trees, one-period state models, two-asset portfolios, discrete loss distributions) run on lmfinance without
typing constants such as ln(1.10) or doing the finance in page arithmetic. Each is a pure function of its arguments
(value structs, like the rest of the host), has a schema entry (generated from `cpp/main.cpp`), code in `cpp/finance.cpp`,
and a pinned closed-form test in `tests/finance_additions.lms` (run by `test.sh`). Written from the formulas and from
QuantLib classes where one is named; no code of any other tool was used.

`FloatList` is the lmcode standard-library list of floats (`float_list()`, `.add(x)`, `.at(i)`, `.len()`).

## Conventions (read before using)

- **Rates are per period** in the functions that say so (`annuity_*`, `loan_*`, `perpetuity`, `dcf_two_stage`,
  `model.binomial_tree`); **years** everywhere a curve is involved. Compounding names: `simple`, `annual`, `semiannual`,
  `quarterly`, `monthly`, `continuous`.
- `Curve.discount(t)` takes a time, not a date, so a curve built by `flat_curve_compounded` or `discount_curve` gives exact
  period factors on any evaluation date. Date-based objects (bonds, bootstrapped curves) follow the calendar: from
  2024-01-02 the first year is 366 days. To reproduce whole-year numbers exactly through a bond, set the evaluation date to a
  day with no 29 February in the next years (the tests use 2025-01-02).
- `curve` nodes (`zero`, `discount`) are placed on a 365-day-year day grid, so a fractional year is rounded to a day.
- Money is rounded by `lmf.round` / `lmf.money`, never by cutting strings: `1331 / 1.1^3` is `999.9999999999997` in binary
  and `lmf.round(.., 2)` gives exactly `1000`.

## The items

| # | Functions | Notes |
|---|---|---|
| 1 | `lmf.round(x, digits)`, `lmf.money(x)`, `lmf.fraction(x, max_den)`, `lmf.exp(x)`, `lmf.log(x)` | Rounding works on the 15-digit decimal value of `x`, half away from zero, so `1.005` rounds to `1.01` and `2.675` to `2.68` (binary rounding gives `1.00`, `2.67`). `money` prints `"2,486.85"`, `"-1,234.57"` (never `-0.00`). `fraction` by continued fractions: `7.142857142857143` gives `"50/7"`; an integer prints as `"3"`. `log` of a non-positive number is a run error. |
| 2 | `model.binomial_tree(spot, up, down, rate, steps) -> Tree`; `Tree.q()`, `steps()`, `discount()` (one step: 1/(1+r)), `stock(i, j)`, `prob(i, j)`, `state_price(i, j)`, `is_arbitrage_free()` | SIMPLE rate per step, q = (1 + r - d)/(u - d). Building never fails; steps 1..1000. `i` steps, `j` ups, `0 <= j <= i`. |
| 3 | `pricing.tree_price(option, tree) -> TreeResult`; `price()`, `value`, `intrinsic`, `continuation`, `exercised -> bool`, `delta`, `gamma`, `bond` (each `(i, j)`) | Backward induction; the option gives call/put, strike and European/American (`years` is ignored: it expires at the last step). Pricing on a tree that is not arbitrage-free (not d < 1 + r < u) is a run error naming the condition. Gamma is the book's per-node difference quotient `(delta_up - delta_down)/(S_up - S_down)`, which differs from a lattice engine's (0.029630 against 0.028219 for the pinned case). `exercised` is true only when intrinsic is strictly larger than continuation. |
| 4 | `market.flat_curve_compounded(rate, compounding) -> Curve`; `market.equivalent_rate(rate, from, to)` | `FlatForward` with a `Compounding`/`Frequency`; `InterestRate::equivalentRate` over one year. `flat_curve_compounded(0.10, "annual").discount(3.0)` is `0.751314800901578`. |
| 5 | `market.discount_curve() -> CurveBuilder`; `.discount(years, factor)`, `.build()` | `InterpolatedDiscountCurve<LogLinear>`, D(0) = 1, flat-forward extrapolation. |
| 6 | `risk.portfolio_return(w, r)`, `risk.portfolio_variance(w, vols, corr)`, `risk.portfolio_vol(...)` | `corr` is the n by n matrix row by row. Run errors: weights not summing to 1, matrix not symmetric / unit diagonal / entries in [-1, 1]. |
| 7 | `pricing.npv(curve, times, amounts)`, `pricing.pv_terms(...) -> FloatList` | sum of amount * D(t), a flow at t = 0 counts in full (QuantLib's `CashFlows::npv` skips flows on the settlement date; this sums over exact times instead, so a fractional time is not rounded to a date). |
| 8 | `Option.payoff(spot)`, `instrument.forward(delivery_price, years) -> Option` | Plain options and forwards (S - K). Barrier and Asian payoffs depend on the path: a run error. A forward has no option price: `pricing.price` of it is a run error. |
| 9 | `risk.loss_distribution(losses, probs) -> LossDist`; `mean()`, `cdf(x)`, `var(conf)`, `es(conf)` | Positive is a loss. VaR: smallest loss whose cumulative probability reaches the confidence (1e-12 tolerance). ES: mean loss over the worst `1 - conf` of probability, splitting the boundary scenario. Not `Series` (which interpolates). |
| 10 | `instrument.strategy() -> Strategy`; `add(option, qty)`, `payoff(spot)`, `profit(spot, net_premium)`, `payoffs(lo, hi, points) -> FloatList` | profit = payoff - premium (the premium is not compounded). |
| 11 | `risk.min_variance_weights(vols, corr)`, `risk.tangency_weights(returns, vols, corr, risk_free)` | Fully invested, short sales allowed. n = 2 uses the closed form (valid at rho = -1); n > 2 solves with Gaussian elimination and a singular covariance is a run error. |
| 12 | `instrument.swap_simple(notional, fixed_rate, years, freq) -> Swap` | Works with `pricing.swap_npv`, `swap_par_rate`, `swap_annuity`, `risk.swap_dv01`. See "Deviations". |
| 13 | `pricing.swap_fixed_leg_npv`, `swap_float_leg_npv` (positive), `pricing.swap_flows(swap, curve, leg)` with leg `fixed`, `float`, `net` | Also for market swaps (QuantLib legs); `net` only for `swap_simple`. |
| 14 | `Curve.zero_rate(t, compounding)`, `Curve.forward_rate(t1, t2, compounding)` | `YieldTermStructure::zeroRate/forwardRate`. The existing `zero`, `forward` stay continuous. |
| 15 | `pricing.perpetuity(cash, rate, growth)`, `pricing.dcf_two_stage(rate, flows, terminal_growth)` | cash (1 + g)/(r - g), first payment one period out; error unless r > g. Two-stage: flows at t = 1..n plus a growing perpetuity after the last. |
| 16 | `pricing.forward_price(spot, curve, years, income, cost)`, `risk.mark_to_market(entry, settles)` | F = S/D(T) - income + cost; daily cash per unit for a long position. |
| 17 | `pricing.annuity_pv(payment, rate, periods)`, `annuity_fv(...)` | Ordinary annuity (payments at period ends). |
| 18 | `pricing.bond_cashflow_times(bond)`, `bond_cashflow_amounts(bond)`; `instrument.bond_dated(face, coupon, issue, maturity, freq)` | Flows after the evaluation date, same-date flows merged; times are 30/360 years from the evaluation date (a bond from `instrument.bond(1000, 0.10, 3.0, 1)` has times 1, 2, 3 and amounts 100, 100, 1100). `bond_accrued` is per 100 face like the clean price: issue 2024-01-01, 10% semiannual, evaluation date 2024-05-01 gives 3.3333 per 100 (33.33 on a face of 1000). |
| 19 | `pricing.par_yield(curve, years, freq)`; `CurveBuilder.bond(years, coupon, clean_price)` on `market.bootstrap_curve()` | par yield = freq (1 - D_n)/sum D_t. Bonds are annual-coupon, issued and settled on the evaluation date, 30/360 (`FixedRateBondHelper` in a `PiecewiseYieldCurve<Discount, LogLinear>`); may be mixed with deposit and swap quotes. |
| 20 | `pricing.loan_payment(principal, rate, periods)`, `pricing.amortization(principal, rate, periods, column) -> FloatList` (`payment`, `interest`, `principal`, `balance`) | Whole cents: payment rounded once, interest = round(balance * rate) each period, the last payment takes the residual (1000 at 10% over 3 periods: 402.11, 402.11, 402.13). Own code; the cent rounding is `lmf.round`'s. |

## Deviations from the proposal and honest notes

- **Item 7** sums amount * D(t) directly instead of building a QuantLib `Leg` of `SimpleCashFlow`s: a leg needs dates, and a
  date is a whole day, so a time such as 0.5 years would move to 183 days. The result is what `CashFlows::npv` computes on
  whole-year dates.
- **Item 12** is computed from the curve's discount factors at exact times k/freq (own code, a screen) rather than through a
  QuantLib `VanillaSwap`. A QuantLib swap is date-based: its accruals and the curve's times follow the calendar (a leap day
  makes the first year 366 days), so the textbook numbers (value 2.50, par rate 8/135 on the 0.96/0.90/0.84 curve) would only
  appear on an evaluation date with no leap day in the term. Measured with a scratch `VanillaSwap` (explicit annual schedules,
  30/360 legs, an `IborIndex` with 1Y tenor and no fixing lag, the same curve): on 2025-01-02 it gives NPV 2.500000, legs
  13.500000 / 16.000000 and fair rate 0.05925926; on 2024-01-02 it gives 2.518369, 13.497507 / 16.015876 and 0.05932902. The floating flow of a period is N (D(t - 1/f)/D(t) - 1): the
  curve's simple forward times the accrual 1/f. `swaption` refuses a `swap_simple` underlying.
- **Item 18** `bond_accrued` keeps its existing per-100-face unit; the lesson multiplies by face/100.
- Items 2, 3, 6, 9, 11, 15, 16, 17, 20 are own code as proposed; 4, 5, 14, 18 (dated bond), 19 use the named QuantLib classes.

## Examples

```
let c = market.flat_curve_compounded(0.10, "annual");
lmf.print(lmf.money(1331.0 * c.discount(3.0)));               # 1,000.00
lmf.print(lmf.money(pricing.annuity_pv(1000.0, 0.10, 3)));    # 2,486.85
let t = model.binomial_tree(100.0, 1.2, 0.9, 0.05, 2);
lmf.print(str(pricing.tree_price(instrument.american_put(105.0, 1.0), t).price()));   # 7.14285714285714 (50/7)
lmf.print(lmf.fraction(pricing.tree_price(instrument.american_put(105.0, 1.0), t).price(), 1000));   # 50/7
let s = instrument.swap_simple(100.0, 0.05, 3.0, 1);
let d = market.discount_curve().discount(1.0, 0.96).discount(2.0, 0.90).discount(3.0, 0.84).build();
lmf.print(lmf.money(pricing.swap_npv(s, d)) + " " + lmf.fraction(pricing.swap_par_rate(s, d), 1000));   # 2.50 8/135
```

The pinned values are in `tests/finance_additions.lms`; `test.sh` runs it with the other programs.
