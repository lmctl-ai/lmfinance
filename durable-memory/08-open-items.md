# Open items

Done in 0.1.2: barrier (analytic) and Asian (geometric analytic, arithmetic Monte Carlo) options, caps/floors and swaptions
(Black, flat vol, implied vols), `risk.series` (empirical quantile VaR/ES from a file: historical or simulated), and the Heston
calibration raises an error when Levenberg-Marquardt stops without a stationary point.

- Calendars and business-day conventions for bonds; floating-rate bonds, callable bonds.
- Hull-White/short-rate models, multi-curve (OIS discounting), swaption/cap volatility cubes (only flat vols).
- More exotics: double barriers, lookbacks, rebates, Heston or Monte Carlo pricing of barriers.
- Local volatility / vol surfaces; calibration beyond implied vol and Heston.
- `risk.series` reads a file; a series built from DSL values without a file round trip would help.
- Objects are never freed; scoped arenas would help long loops.
- Compile time: a ccache or a prebuilt-QuantLib cache in CI would cut the five jobs.
