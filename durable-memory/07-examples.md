# Examples

`examples/*.lms` each assert their own results and write CSVs under `build/examples/`;
`examples/combo/*.lms` show two hosts in one script (lmfinance computes, lmplot draws). Notes:

- The DSL has no `exp`/`log`, no ternary, and numbers print through `str()`; build CSV text in a loop
  and write it with `lmf.save_text`, or use `Curve.save_csv` / `risk.save_profile`.
- Bootstrapped curves have piecewise-constant forwards (log-linear discount factors): the picture in
  the README shows the steps.
- `system` in plain lmscript returns `{code, stdout, stderr}`; check `r.code`.

## Gallery (pictures) and `lmctl autorun`

`examples/gallery/*.lms` (10 programs) compute with lmfinance, write CSV/text into `data/` and draw with lmplot into `images/` in the
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
