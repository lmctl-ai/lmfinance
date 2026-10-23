# Open items

- Calendars and business-day conventions for bonds; floating-rate bonds, callable bonds.
- Caps/floors, swaptions, Hull-White/short-rate models, multi-curve (OIS discounting).
- Barrier, Asian and other exotics (QuantLib has the engines; the schema needs payoff builders).
- Local volatility / vol surfaces; calibration beyond implied vol and Heston.
- Historical and Monte Carlo VaR (needs data series objects).
- Objects are never freed; scoped arenas would help long loops.
- Compile time: a ccache or a prebuilt-QuantLib cache in CI would cut the five jobs.
