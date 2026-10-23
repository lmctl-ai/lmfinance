# Examples

`examples/*.lms` each assert their own results and write CSVs under `build/examples/`;
`examples/combo/*.lms` show two hosts in one script (lmfinance computes, lmplot draws). Notes:

- The DSL has no `exp`/`log`, no ternary, and numbers print through `str()`; build CSV text in a loop
  and write it with `lmf.save_text`, or use `Curve.save_csv` / `risk.save_profile`.
- Bootstrapped curves have piecewise-constant forwards (log-linear discount factors): the picture in
  the README shows the steps.
- `system` in plain lmscript returns `{code, stdout, stderr}`; check `r.code`.

## Gallery (pictures) and `lmctl autorun`

`examples/gallery/*.lms` (28 programs) compute with lmfinance, write CSV/text into `data/` and draw with lmplot or lmdraw into `images/` in the
*current directory*; the README shows copies in `docs/images/` (`examples/render-all.sh` runs them from a checkout and copies;
PNG <= 300 KB, GIF <= 3 MB). All examples, old and new, end with `r = system ${dsl_cli(p)} --dsl ${out}; assert(r.code == 0);` so
`lmctl autorun <raw github url>` produces output; README captions are those commands. Old examples `mkdir("build/examples")` first.
- Stories: bootstrapped curve, call price/delta, option value melting onto its payoff (40-frame GIF), American vs European put
  (early-exercise premium), bond price-yield convexity with the duration tangent, parallel-shock P/L of a bond and a swap, Heston
  smile for three correlations, engine convergence (tree, FD, MC), seeded MC inside its error bars, VaR/ES tail.
- lmplot habits: `&` is dropped from titles (write P/L), no inline `-` data, read single numbers back into gnuplot with
  `stats 'file' u 1:2 nooutput` (never `system('head -1')`: not portable to Windows; run stats before ranges and log scales), `set format x '10^{%L}'` on log axes; the DSL has no exponent literals and `lmf` has `check`, not `assert_true`;
  `pricing.monte_carlo` takes at most 1e8 paths.
- `test.sh` checks README images (magic bytes, sizes, autorun captions) and runs the gallery when lmplot is available (GIF needs ffmpeg).
- Added in the QA pass: `option_strategies` (straddle/bull spread/butterfly against payoffs; parity C - P = S - K e^(-rT) holds
  only to a cent because QuantLib rounds maturities to whole days), `gamma_heatmap` (analytic gamma vs N'(d1)/(S sigma sqrt T)),
  `vol_surface` (Heston prices inverted with `calibration.implied_vol` over 17 strikes x 12 maturities; skew and rising term
  structure asserted), `bond_balance` (lmdraw: PV bars sum to `pricing.bond_npv` within 10 cents, fulcrum = Macaulay duration),
  `dv01_ladder` (zero-bond DV01 = P T 1bp within 0.5 %, swap DV01 = annuity x 1bp within 5 %).
- Pipeline notes: grids go to a file with blank lines between rows for `pm3d map`; lmscript reads CSV with `csv_parse(read_file(..))`
  (write a header line from the DSL with `lmf.save_text`); lmdraw writes only under `build/rendered/` (copy_file to `images/`) and
  its `end _dsl_` must be at column 1; `${x}` of an integral number is an int literal (add + 0.0001 for floats).

## Added in the 0.1.2 pass (13 examples)

- Existing API: `curve_shapes` (a payer swap is worth MORE on an inverted curve: its first five years carry the highest forwards),
  `bond_ladder` (par bond at 100 for every maturity, zero-bond Macaulay = T), `greeks_surface` (41 x 40 analytic grid, pm3d maps:
  do not `unset surface` or `set view map` before `splot ... w pm3d`), `tree_convergence` (CRR at 200 steps is still 1e-2 off,
  Leisen-Reimer 201 steps within 1e-5), `exercise_boundary` (bisection in lmscript over CRR prices; S*(T) starts near K and stays
  above the perpetual limit K g/(1+g), g = 2r/vol^2), `heston_calibration` (36 quotes, wrong guess, rms < 1e-4), `portfolio_waterfall`
  (candlesticks as waterfall boxes), `swap_cashflows` (lmdraw; the legs rebuilt from forwards agree with `swap_npv` to 0.1% of notional).
- New API (0.1.2): `barrier_options` (in-out parity; the up-and-out call's vega turns negative), `asian_options` (arithmetic MC with
  the geometric control variate: 40 000 paths x 8 fixing counts x 21 strikes is 17 s), `caps_floors` (cap = floor at the ATM rate,
  cap - floor linear in K), `swaption_grid` (ATM payer = receiver; annuity from two strikes), `mc_var` (three hosts: lmnum normals and
  `cholesky`, lmfinance repricing, `risk.series` VaR/ES; the book is net long gamma so full-revaluation VaR < delta-normal).
- gnuplot lessons: `index` blocks need TWO blank lines (one blank line only separates `every` blocks); `set key below` plus a
  later `set key top left` in another panel raises "ignoring top/center/bottom; incompatible with tmargin/bmargin" (lmplot treats
  gnuplot warnings as errors); key placement in multiplot panels is most reliable with `set key at graph x, y <corner>`;
  `lw` before `ls` is "contradicting arguments"; `%+.1f` and `%%` are not accepted by lmscript's `sprintf`.
