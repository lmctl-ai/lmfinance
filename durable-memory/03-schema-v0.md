# Schema v0

Seven contexts, each a type with no state of its own (the host object behind them is shared):
`market` MarketApi, `instrument` InstrumentApi, `model` ModelApi, `pricing` PricingApi,
`risk` RiskApi, `calibration` CalibrationApi, `lmf` LmfApi. Returned object types: `Curve`,
`CurveBuilder`, `Option`, `Bond`, `Swap`, `Model`, `Engine`, `Result`, `HestonFit`.
`schema/lmfinance.dsl.json` is generated from the `method(r, ...)` registrations in `cpp/main.cpp`
by `scripts/gen-schema.py`; `test.sh` fails when it is stale and diffs it against `lmscript
--dsl-signatures`. The DSL has `pow`, `sqrt`, `abs`, `str`, `float`, `for (i in range(a, b))` and
string `+`, but no `exp`/`log` (use `pow(2.718281828459045, x)`), no ternary and no bool asserts
(hence `lmf.check(bool, message)`).

## Conventions

- Rates are decimals; vol is annual; maturities are years; one year is 365 days (Actual/365F).
  An option with `years = T` expires `round(365 T)` days after the evaluation date.
- **Evaluation date**: host state, default 2024-01-02 (a TARGET business day), set by
  `market.set_date`. Set it before building curves: curves are anchored to the date they were built at.
- **Curves**: `flat_curve(r)` is continuously compounded. `zero_curve()` takes `(years, zero)` nodes,
  linear in the zero rate, flat extrapolation from the first node. `bootstrap_curve()` takes
  `deposit("3M", r)` (Act/360, 2 days spot, TARGET) and `swap("5Y", r)` (annual 30/360 fixed vs
  Euribor 6M, TARGET, Modified Following) quotes: PiecewiseYieldCurve, log-linear discount factors
  (so forwards are piecewise constant). Swaps priced by `pricing.swap_*` use the same conventions on
  the curve as both discount and forward curve, so a bootstrapped curve reprices its swaps exactly.
  `Curve.bumped(dr)` adds `dr` to every continuously compounded zero rate.
- **Bonds**: issued on the evaluation date, settlement the same day, null calendar, unadjusted,
  30/360 (bond basis). `bond(face, coupon, years, freq)`: freq 1, 2, 4 or 12, maturity a whole number
  of coupon periods. Yields are compounded with the coupon frequency (annual for a zero bond).
  Prices are clean per 100 face except `bond_npv` (dirty, currency). Duration is modified unless the
  name says Macaulay. `risk.bond_dv01` is the price drop (currency) per +1bp of continuous zero rate.
- **Swaps**: payer by default (pays fixed), notional, fixed rate, whole years. `risk.swap_dv01` is the
  NPV change for +1bp, signed (positive for a payer).
- **Options**: `Option` is a payoff and exercise; `Model` is a Black-Scholes-Merton or Heston market;
  `Engine` is a numerical method; `pricing.price` combines them. Greeks of a `Result` are those the
  engine returns (analytic: all; binomial and finite differences: delta, gamma, theta; Monte Carlo and
  BAW: none beyond the price and, for MC, `stderr`) — asking for another is a run error pointing at
  the `risk` functions. Units: vega per 1.00 of vol, rho per 1.00 of rate, theta per year.
  `risk.*` are central finite differences (spot 1% bump, vol 0.001, rate 0.0001, one day for theta)
  on any engine; Monte Carlo uses the same seed so the bumps share random numbers.
- **Monte Carlo**: QuantLib pseudo-random (Mersenne Twister + inverse cumulative normal),
  antithetic, one time step. The seed must be >= 1 (0 means a clock seed in QuantLib).
- **Calibration**: `implied_vol` is Brent in [1e-4, 5] on the chosen engine; Heston fit is
  Levenberg-Marquardt on implied-vol errors with the analytic Heston engine.
- Errors from QuantLib surface as run errors (exit 3) with QuantLib's message.
