// lmfinance: the QuantLib-facing layer. Plain value structs describe curves, instruments, models and
// engines; every pricing call builds the QuantLib objects it needs from them, so nothing in the host
// holds QuantLib state across calls except the (immutable) yield curves and the evaluation date.
#pragma once
#include <ql/termstructures/yieldtermstructure.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lmfinance {

struct Clock { QuantLib::Date today; };  // the evaluation date; never the system clock

struct Curve { QuantLib::ext::shared_ptr<QuantLib::YieldTermStructure> ts; };
struct CurveBuilder {
  bool bootstrap = false;
  std::vector<std::pair<double, double>> zeros;                     // years, continuous zero rate
  std::vector<std::pair<std::string, double>> deposits, swaps;      // tenor, par rate
};
enum class Exotic { None, Barrier, Asian };
struct Option {
  bool call = true, american = false; double strike = 0, years = 0;
  Exotic exotic = Exotic::None;
  std::string barrier_type;                                         // up-in, up-out, down-in, down-out
  double barrier = 0, rebate = 0;
  std::string averaging;                                            // geometric or arithmetic (discrete, equally spaced)
  int fixings = 0;
};
struct Bond { double face = 100, coupon = 0, years = 0; int freq = 1; bool zero = false; };
struct Swap { double notional = 1, rate = 0, years = 0; bool receiver = false; double spread = 0; };
struct CapFloor { bool cap = true; double notional = 1, strike = 0, years = 0; };
struct Swaption { Swap swap; double expiry = 0; };                  // option to enter `swap` in `expiry` years
struct Series { std::vector<double> v; };                           // profit-and-loss or return samples
struct Model {
  bool heston = false;
  double spot = 0, r = 0, q = 0, vol = 0;                           // Black-Scholes-Merton
  double v0 = 0, kappa = 0, theta = 0, sigma = 0, rho = 0;          // Heston
};
enum class EngineKind { Analytic, Binomial, FiniteDifference, MonteCarlo, BaroneAdesiWhaley };
struct Engine { EngineKind kind = EngineKind::Analytic; int steps = 0, grid = 0; int64_t paths = 0, seed = 0; std::string tree = "crr"; };
struct Result {
  double price = 0, stderr_ = NAN, delta = NAN, gamma = NAN, vega = NAN, theta = NAN, rho = NAN;
};
struct HestonFit {
  double spot = 0, r = 0, q = 0, v0 = 0.04, kappa = 1.5, theta = 0.04, sigma = 0.5, rho = -0.5, error = NAN;
  struct Quote { double strike, years, vol; };
  std::vector<Quote> quotes;
};

QuantLib::Date parse_date(const std::string &iso);
std::string format_date(QuantLib::Date d);

Curve flat_curve(const Clock &, double rate);
Curve build_curve(const Clock &, const CurveBuilder &);
Curve bump_curve(const Curve &, double rate_shift);
double curve_discount(const Curve &, double t);
double curve_zero(const Curve &, double t);
double curve_forward(const Curve &, double t1, double t2);
void curve_save_csv(const Curve &, const std::string &path, double tmax, int n);

Result price_option(const Clock &, const Option &, const Model &, const Engine &);
double bumped_delta(const Clock &, const Option &, const Model &, const Engine &);
double bumped_gamma(const Clock &, const Option &, const Model &, const Engine &);
double bumped_vega(const Clock &, const Option &, const Model &, const Engine &);
double bumped_rho(const Clock &, const Option &, const Model &, const Engine &);
double bumped_theta(const Clock &, const Option &, const Model &, const Engine &);
void save_profile(const Clock &, const Option &, const Model &, const Engine &, double lo, double hi, int n, const std::string &path);

double bond_clean_price(const Clock &, const Bond &, const Curve &);
double bond_npv(const Clock &, const Bond &, const Curve &);
double bond_accrued(const Clock &, const Bond &);
double bond_clean_from_yield(const Clock &, const Bond &, double y);
double bond_yield(const Clock &, const Bond &, double clean);
double bond_duration(const Clock &, const Bond &, double y, bool macaulay);
double bond_convexity(const Clock &, const Bond &, double y);

double swap_npv(const Clock &, const Swap &, const Curve &);
double swap_par_rate(const Clock &, const Swap &, const Curve &);
double swap_annuity(const Clock &, const Swap &, const Curve &);

// Black (lognormal, flat volatility) prices of caps/floors on Euribor 6M and of swaptions on the swap conventions above.
double capfloor_npv(const Clock &, const CapFloor &, const Curve &, double vol);
double capfloor_implied_vol(const Clock &, const CapFloor &, const Curve &, double price);
double capfloor_atm_rate(const Clock &, const CapFloor &, const Curve &);
double swaption_npv(const Clock &, const Swaption &, const Curve &, double vol);
double swaption_implied_vol(const Clock &, const Swaption &, const Curve &, double price);
double swaption_forward_rate(const Clock &, const Swaption &, const Curve &);

// Empirical risk from samples: whitespace/comma separated numbers in a file (one header line allowed).
Series load_series(const std::string &path);
double series_quantile(const Series &, double p);                   // linear interpolation between order statistics
double series_var(const Series &, double confidence);               // loss (positive) exceeded with probability 1 - confidence
double series_es(const Series &, double confidence);                // mean loss beyond that quantile

double implied_vol(const Clock &, const Option &, const Model &, const Engine &, double price);
void heston_fit(const Clock &, HestonFit &);
double normal_quantile(double p);
double normal_density(double x);

}  // namespace lmfinance
