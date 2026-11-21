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
  bool discount = false;                                            // market.discount_curve(): nodes are discount factors
  std::vector<std::pair<double, double>> discounts;                 // years, discount factor
  std::vector<std::pair<double, double>> zeros;                     // years, continuous zero rate
  std::vector<std::pair<std::string, double>> deposits, swaps;      // tenor, par rate
  struct BondQuote { double years, coupon, clean; };
  std::vector<BondQuote> bonds;                                     // annual-coupon bonds issued today, clean price per 100
};
enum class Exotic { None, Barrier, Asian, Forward };
struct Option {
  bool call = true, american = false; double strike = 0, years = 0;
  Exotic exotic = Exotic::None;
  std::string barrier_type;                                         // up-in, up-out, down-in, down-out
  double barrier = 0, rebate = 0;
  std::string averaging;                                            // geometric or arithmetic (discrete, equally spaced)
  int fixings = 0;
};
struct Bond { double face = 100, coupon = 0, years = 0; int freq = 1; bool zero = false; bool dated = false; std::string issue, maturity; };
struct Swap { double notional = 1, rate = 0, years = 0; bool receiver = false; double spread = 0; bool simple = false; int freq = 1; };
struct CapFloor { bool cap = true; double notional = 1, strike = 0, years = 0; };
struct Swaption { Swap swap; double expiry = 0; };                  // option to enter `swap` in `expiry` years
struct Series { std::vector<double> v; };                           // profit-and-loss or return samples
struct Strategy;
struct Model {
  bool heston = false;
  double spot = 0, r = 0, q = 0, vol = 0;                           // Black-Scholes-Merton
  double v0 = 0, kappa = 0, theta = 0, sigma = 0, rho = 0;          // Heston
};
enum class EngineKind { Analytic, Binomial, FiniteDifference, MonteCarlo, BaroneAdesiWhaley };
struct Engine {
  EngineKind kind = EngineKind::Analytic; int steps = 0, grid = 0; int64_t paths = 0, seed = 0; std::string tree = "crr";
  bool plain = false;                                               // Monte Carlo without antithetic and control variates
  bool stepped = false;                                             // Monte Carlo on `steps` time steps (barrier options)
};
struct Result {
  double price = 0, stderr_ = NAN, delta = NAN, gamma = NAN, vega = NAN, theta = NAN, rho = NAN;
};
struct Strategy { std::vector<std::pair<Option, double>> legs; };   // option, signed quantity
struct Tree { double spot = 0, up = 1, down = 1, rate = 0; int steps = 1; };   // explicit binomial tree, SIMPLE rate per step
struct TreeResult {
  bool call = true, american = false; double strike = 0, q = 0, rate = 0; int steps = 0;
  std::vector<std::vector<double>> stock, value, intrinsic, continuation;     // [step][ups]
  std::vector<std::vector<char>> exercised;
};
struct LossDist { std::vector<std::pair<double, double>> v; };       // (loss, probability), ascending in loss
struct HestonFit {
  double spot = 0, r = 0, q = 0, v0 = 0.04, kappa = 1.5, theta = 0.04, sigma = 0.5, rho = -0.5, error = NAN;
  struct Quote { double strike, years, vol; };
  std::vector<Quote> quotes;
  std::vector<double> errors;                                       // after fit(): model implied vol - quoted vol, in quote order
  int evaluations = -1;                                             // after fit(): cost-function evaluations
  std::string reason;                                               // after fit(): the end criterion, as text
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

// ---- additions for the finance track (docs: durable-memory/12-finance-track-additions.md) ----
// 1. number helpers
double round_decimal(double x, int digits);                         // half away from zero on the 15-digit decimal value of x
std::string money_text(double x);                                   // "1,331.00"
std::string fraction_text(double x, int64_t max_denominator);       // "50/7"
double math_exp(double x);
double math_log(double x);
// 4. compounding
Curve flat_curve_compounded(const Clock &, double rate, const std::string &compounding);
double equivalent_rate(double rate, const std::string &from, const std::string &to);
// 7. present value of a list of cash flows
double npv(const Curve &, const std::vector<double> &times, const std::vector<double> &amounts);
std::vector<double> pv_terms(const Curve &, const std::vector<double> &times, const std::vector<double> &amounts);
// 17. annuities, 20. loans
double annuity_pv(double payment, double rate, int64_t periods);
double annuity_fv(double payment, double rate, int64_t periods);
double loan_payment(double principal, double rate, int64_t periods);
std::vector<double> amortization(double principal, double rate, int64_t periods, const std::string &column);

// 5. curve from discount factors; 14. rates with a stated compounding
// (build_curve handles CurveBuilder::discount)
double curve_zero_rate(const Curve &, double t, const std::string &compounding);
double curve_forward_rate(const Curve &, double t1, double t2, const std::string &compounding);
// 8. payoff at expiry of a plain option or a forward; 10. strategy of weighted legs
double option_payoff(const Option &, double spot);
double strategy_payoff(const Strategy &, double spot);
std::vector<double> strategy_payoffs(const Strategy &, double lo, double hi, int points);
// 15. perpetuity; 16. forward price with carry, daily settlement
double perpetuity(double cash, double rate, double growth);
double dcf_two_stage(double rate, const std::vector<double> &flows, double terminal_growth);
double forward_price(const Curve &, double spot, double years, double income, double cost);
std::vector<double> mark_to_market(double entry, const std::vector<double> &settles);
// 2. explicit binomial tree; 3. pricing on it
double tree_q(const Tree &);
double tree_stock(const Tree &, int step, int ups);
double tree_prob(const Tree &, int step, int ups);
double tree_state_price(const Tree &, int step, int ups);
bool tree_arbitrage_free(const Tree &);
TreeResult tree_price(const Option &, const Tree &);
double tree_result_value(const TreeResult &, const char *what, int step, int ups);
double tree_result_delta(const TreeResult &, int step, int ups);
double tree_result_gamma(const TreeResult &, int step, int ups);
double tree_result_bond(const TreeResult &, int step, int ups);

// 6. portfolio moments, 11. minimum-variance and tangency weights
double portfolio_return(const std::vector<double> &w, const std::vector<double> &r);
double portfolio_variance(const std::vector<double> &w, const std::vector<double> &vols, const std::vector<double> &corr);
std::vector<double> min_variance_weights(const std::vector<double> &vols, const std::vector<double> &corr);
std::vector<double> tangency_weights(const std::vector<double> &mu, const std::vector<double> &vols, const std::vector<double> &corr, double rf);
// 9. discrete loss distribution
LossDist loss_distribution(const std::vector<double> &losses, const std::vector<double> &probs);
double loss_mean(const LossDist &);
double loss_cdf(const LossDist &, double x);
double loss_var(const LossDist &, double conf);
double loss_es(const LossDist &, double conf);
// 12-13. swap with textbook conventions: legs, flows
double swap_fixed_leg_npv(const Clock &, const Swap &, const Curve &);
double swap_float_leg_npv(const Clock &, const Swap &, const Curve &);
std::vector<double> swap_flows(const Clock &, const Swap &, const Curve &, const std::string &leg);
// 18-19. bond cash flows, dated bond, par yield
std::vector<double> bond_cashflow_times(const Clock &, const Bond &);
std::vector<double> bond_cashflow_amounts(const Clock &, const Bond &);
double par_yield(const Curve &, double years, int freq);

double implied_vol(const Clock &, const Option &, const Model &, const Engine &, double price);
void heston_fit(const Clock &, HestonFit &);
double normal_quantile(double p);
double normal_density(double x);

// ---- wave 5 additions (docs: durable-memory/13-finance-wave5-additions.md) ----
inline constexpr int64_t mc_max_paths = 4000000;                    // ceiling on paths, paths x fixings, paths x steps
// 1. standard normal distribution
double norm_cdf(double x);
double norm_pdf(double x);
double norm_inv(double p);
// 2. the d1 and d2 of the analytic European engine (same day-count time, same forward)
double bs_d1(const Clock &, const Option &, const Model &);
double bs_d2(const Clock &, const Option &, const Model &);
// 4. risk-neutral Black-Scholes samples; 5. payoff of an option on a given path
std::vector<double> terminal_samples(const Model &, double years, int64_t n, int64_t seed);
std::vector<double> sample_path(const Model &, double years, int64_t steps, int64_t seed);
double path_payoff(const Option &, const std::vector<double> &path);
// 7. Black (1976) and Bachelier formulas on a forward
double black76(const std::string &kind, double forward, double strike, double vol, double expiry, double discount);
double bachelier(const std::string &kind, double forward, double strike, double normal_vol, double expiry, double discount);
// 8. the pieces of a cap/floor and of a swaption
std::vector<double> capfloor_caplets(const Clock &, const CapFloor &, const Curve &, double vol);
std::vector<double> capfloor_forwards(const Clock &, const CapFloor &, const Curve &);
double capfloor_swap_npv(const Clock &, const CapFloor &, const Curve &);
double swaption_annuity(const Clock &, const Swaption &, const Curve &);
// 9. implied-volatility smile of a model
std::vector<double> smile(const Clock &, const Model &, const std::vector<double> &strikes, double years);
// 10. textbook yield DV01 (currency, for the bond's face)
double bond_dv01_yield(const Clock &, const Bond &, double y);

}  // namespace lmfinance
