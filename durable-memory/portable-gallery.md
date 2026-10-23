# Portable numeric markers in gallery plots

var_tail, bond_convexity and mc_convergence read their marker values with gnuplot stats, without shell commands. Run stats before setting plot ranges or log scales: those settings also filter stats input (notably negative VaR values and the duration below the bond price range).

All three scripts passed lmctl autorun in separate fresh directories against published @latest hosts on 2026-10-23; each generated PNG was byte-identical to its committed gallery image. The CI gallery loop runs these scripts on all five platforms, including Windows.
