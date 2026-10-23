# Numerics and validation

Everything the tests assert is a closed form or a literature value, never a value taken from the
implementation itself:

- Black-Scholes S=100 K=100 r=5% vol=20% T=1: call 10.4506, put 5.5735, call delta 0.636831, gamma
  0.018762, vega 37.5240, theta -6.4140, rho 53.2325. Hull's example (S=42 K=40 r=10% vol=20% T=0.5):
  4.7657 and 0.8096 (Hull rounds to 4.76, 0.81). Put-call parity with and without dividend yield.
- American put on the same market: converged value 6.0903 (CRR 1000 steps 6.0896, 5000 steps 6.0902,
  Leisen-Reimer 1001 steps 6.0901, FD 200x400 6.0874, Barone-Adesi-Whaley 6.0976). Leisen-Reimer
  needs an odd step count and a large one; 501 steps was off by 0.009.
- Bonds: par bond at its coupon, Macaulay duration (1+y)/y (1-(1+y)^-n) = 8.1078 for 10y 5%,
  annuity formula price 92.639913 at 6%, zero bond 100/1.04^5 = 82.192711.
- Flat curve: D = exp(-rt); a zero bond on a flat curve is the discount factor at the actual day
  count (2024-01-02 + 5y = 1827 days because of two leap days).
- Bootstrap round trip: curve built from deposits and swaps reprices swaps to 1e-7 in rate.
- Heston with vol of vol 1e-4 and v0 = theta = 0.04 equals Black-Scholes 20% (10.4506); parity holds
  for any parameters; the downside skew appears with rho < 0; calibration from a synthetic smile
  recovers v0, kappa, theta, sigma, rho to ~1e-8 from 21 strikes (and to 2% from 15 quotes).
- Monte Carlo: within 4 standard errors, the standard error scales as 1/sqrt(N), same seed gives the
  same bits, different seeds differ.
- VaR: 99% quantile 2.3263, ES 2.6652 per unit sigma.

DV01 note: `bond_dv01` bumps the continuously compounded curve, so it equals *Macaulay* duration x
price x 1bp, not modified duration (the test uses that, which caught a wrong first guess).
