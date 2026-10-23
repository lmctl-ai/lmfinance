# Examples

`examples/*.lms` each assert their own results and write CSVs under `build/examples/`;
`examples/combo/*.lms` show two hosts in one script (lmfinance computes, lmplot draws). Notes:

- The DSL has no `exp`/`log`, no ternary, and numbers print through `str()`; build CSV text in a loop
  and write it with `lmf.save_text`, or use `Curve.save_csv` / `risk.save_profile`.
- Bootstrapped curves have piecewise-constant forwards (log-linear discount factors): the picture in
  the README shows the steps.
- `system` in plain lmscript returns `{code, stdout, stderr}`; check `r.code`.

## Gallery (pictures) and `lmctl autorun`

`examples/gallery/*.lms` (15 programs) compute with lmfinance, write CSV/text into `data/` and draw with lmplot into `images/` in the
*current directory*; the README shows copies in `docs/images/` (`examples/render-all.sh` runs them from a checkout and copies;
PNG <= 300 KB, GIF <= 3 MB). All examples, old and new, end with `r = system ${dsl_cli(p)} --dsl ${out}; assert(r.code == 0);` so
`lmctl autorun <raw github url>` produces output; README captions are those commands. Old examples `mkdir("build/examples")` first.
- Stories: bootstrapped curve, call price/delta, option value melting onto its payoff (40-frame GIF), American vs European put
  (early-exercise premium), bond price-yield convexity with the duration tangent, parallel-shock P/L of a bond and a swap, Heston
  smile for three correlations, engine convergence (tree, FD, MC), seeded MC inside its error bars, VaR/ES tail.
- lmplot habits: `&` is dropped from titles (write P/L), no inline `-` data, read single numbers back into gnuplot with
  `system('head -1 file')`, `set format x '10^{%L}'` on log axes; the DSL has no exponent literals and `lmf` has `check`, not `assert_true`;
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
