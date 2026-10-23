# Examples

`examples/*.lms` each assert their own results and write CSVs under `build/examples/`;
`examples/combo/*.lms` show two hosts in one script (lmfinance computes, lmplot draws). Notes:

- The DSL has no `exp`/`log`, no ternary, and numbers print through `str()`; build CSV text in a loop
  and write it with `lmf.save_text`, or use `Curve.save_csv` / `risk.save_profile`.
- Bootstrapped curves have piecewise-constant forwards (log-linear discount factors): the picture in
  the README shows the steps.
- `system` in plain lmscript returns `{code, stdout, stderr}`; check `r.code`.
