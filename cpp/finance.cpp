#include "finance.hpp"
#include "memory_budget.hpp"

#include <ql/cashflows/cashflows.hpp>
#include <ql/exercise.hpp>
#include <ql/indexes/ibor/euribor.hpp>
#include <ql/instruments/bonds/fixedratebond.hpp>
#include <ql/instruments/bonds/zerocouponbond.hpp>
#include <ql/instruments/makevanillaswap.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <ql/instruments/vanillaswap.hpp>
#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/math/optimization/levenbergmarquardt.hpp>
#include <ql/math/solvers1d/brent.hpp>
#include <ql/models/equity/hestonmodel.hpp>
#include <ql/models/equity/hestonmodelhelper.hpp>
#include <ql/pricingengines/bond/bondfunctions.hpp>
#include <ql/pricingengines/bond/discountingbondengine.hpp>
#include <ql/pricingengines/swap/discountingswapengine.hpp>
#include <ql/pricingengines/vanilla/analyticeuropeanengine.hpp>
#include <ql/pricingengines/vanilla/analytichestonengine.hpp>
#include <ql/pricingengines/vanilla/baroneadesiwhaleyengine.hpp>
#include <ql/pricingengines/vanilla/binomialengine.hpp>
#include <ql/pricingengines/vanilla/fdblackscholesvanillaengine.hpp>
#include <ql/pricingengines/vanilla/mceuropeanengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/processes/hestonprocess.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/settings.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/termstructures/yield/piecewiseyieldcurve.hpp>
#include <ql/termstructures/yield/ratehelpers.hpp>
#include <ql/termstructures/yield/zerocurve.hpp>
#include <ql/termstructures/yield/zerospreadedtermstructure.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/calendars/target.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>
#include <ql/time/daycounters/thirty360.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace lmfinance {
using namespace QuantLib;

namespace {
Calendar null_cal() { return NullCalendar(); }
DayCounter act365() { return Actual365Fixed(); }
DayCounter bond_dc() { return Thirty360(Thirty360::BondBasis); }
void activate(const Clock &c) { Settings::instance().evaluationDate() = c.today; }
Date years_ahead(const Clock &c, double years) {
  if (!(years > 0) || years > 100) QL_FAIL("maturity must be between 0 and 100 years, got " << years);
  // Dates are whole days: a maturity that rounds to the evaluation date would price as already expired (zero).
  const auto days = std::llround(years * 365.0);
  if (days < 1) QL_FAIL("maturity " << years << " years is shorter than one day (1/365 year)");
  return c.today + static_cast<Integer>(days);
}
Handle<YieldTermStructure> flat(const Clock &c, double r) {
  return Handle<YieldTermStructure>(ext::make_shared<FlatForward>(c.today, r, act365()));
}
Period parse_tenor(const std::string &s) {
  if (s.size() < 2) QL_FAIL("bad tenor '" << s << "': use 3M, 6M, 1Y, 10Y, 2W or 30D");
  char u = static_cast<char>(std::toupper(static_cast<unsigned char>(s.back())));
  size_t used = 0;
  int n = 0;
  try { n = std::stoi(s.substr(0, s.size() - 1), &used); } catch (...) { used = 0; }
  if (used != s.size() - 1 || n <= 0) QL_FAIL("bad tenor '" << s << "': use 3M, 6M, 1Y, 10Y, 2W or 30D");
  switch (u) {
    case 'D': return Period(n, Days);
    case 'W': return Period(n, Weeks);
    case 'M': return Period(n, Months);
    case 'Y': return Period(n, Years);
  }
  QL_FAIL("bad tenor '" << s << "': unit must be D, W, M or Y");
}
Frequency frequency(int f) {
  switch (f) {
    case 1: return Annual;
    case 2: return Semiannual;
    case 4: return Quarterly;
    case 12: return Monthly;
  }
  QL_FAIL("coupon frequency must be 1, 2, 4 or 12 per year, got " << f);
}

ext::shared_ptr<GeneralizedBlackScholesProcess> bs_process(const Clock &c, const Model &m) {
  QL_REQUIRE(!m.heston, "this operation needs a Black-Scholes model, not a Heston model");
  Handle<Quote> spot(ext::make_shared<SimpleQuote>(m.spot));
  Handle<BlackVolTermStructure> vol(ext::make_shared<BlackConstantVol>(c.today, null_cal(), m.vol, act365()));
  return ext::make_shared<BlackScholesMertonProcess>(spot, flat(c, m.q), flat(c, m.r), vol);
}
ext::shared_ptr<HestonProcess> heston_process(const Clock &c, const Model &m) {
  return ext::make_shared<HestonProcess>(flat(c, m.r), flat(c, m.q), Handle<Quote>(ext::make_shared<SimpleQuote>(m.spot)),
                                         m.v0, m.kappa, m.theta, m.sigma, m.rho);
}
void check_model(const Model &m) {
  QL_REQUIRE(m.spot > 0, "spot must be positive");
  if (m.heston) {
    QL_REQUIRE(m.v0 > 0 && m.kappa > 0 && m.theta > 0 && m.sigma > 0, "Heston v0, kappa, theta and sigma must be positive");
    QL_REQUIRE(m.rho > -1 && m.rho < 1, "Heston rho must be inside (-1, 1)");
  } else {
    QL_REQUIRE(m.vol > 0, "volatility must be positive");
  }
}

template <class Tree> ext::shared_ptr<PricingEngine> tree_engine(const ext::shared_ptr<GeneralizedBlackScholesProcess> &p, Size n) {
  return ext::make_shared<BinomialVanillaEngine<Tree>>(p, n);
}

Real greek(const std::function<Real()> &f) {
  try { return f(); } catch (const Error &) { return NAN; }
}
}  // namespace

// ---------------------------------------------------------------- dates
Date parse_date(const std::string &s) {
  int y = 0, mo = 0, d = 0;
  char tail = 0;
  if (std::sscanf(s.c_str(), "%d-%d-%d%c", &y, &mo, &d, &tail) != 3) QL_FAIL("bad date '" << s << "': use YYYY-MM-DD");
  QL_REQUIRE(y >= 1901 && y <= 2199 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31, "date '" << s << "' is out of range");
  QL_REQUIRE(d <= Date::endOfMonth(Date(1, static_cast<Month>(mo), y)).dayOfMonth(), "date '" << s << "' does not exist");
  return Date(d, static_cast<Month>(mo), y);
}
std::string format_date(Date d) {
  char b[16];
  std::snprintf(b, sizeof b, "%04d-%02d-%02d", static_cast<int>(d.year()), static_cast<int>(d.month()), static_cast<int>(d.dayOfMonth()));
  return b;
}

// ---------------------------------------------------------------- curves
Curve flat_curve(const Clock &c, double rate) {
  activate(c);
  auto ts = ext::make_shared<FlatForward>(c.today, rate, act365());
  ts->enableExtrapolation();
  return Curve{ts};
}

Curve build_curve(const Clock &c, const CurveBuilder &b) {
  activate(c);
  if (!b.bootstrap) {
    QL_REQUIRE(!b.zeros.empty(), "the curve needs at least one zero() node");
    auto nodes = b.zeros;
    std::sort(nodes.begin(), nodes.end());
    std::vector<Date> dates{c.today};
    std::vector<Rate> rates{nodes.front().second};
    for (const auto &n : nodes) {
      Date d = years_ahead(c, n.first);
      QL_REQUIRE(d > dates.back(), "zero() nodes must be at distinct maturities (at least one day apart)");
      dates.push_back(d);
      rates.push_back(n.second);
    }
    auto ts = ext::make_shared<ZeroCurve>(dates, rates, act365(), null_cal(), Linear(), Continuous);
    ts->enableExtrapolation();
    return Curve{ts};
  }
  QL_REQUIRE(!b.deposits.empty() || !b.swaps.empty(), "the curve needs at least one deposit() or swap() quote");
  std::vector<ext::shared_ptr<RateHelper>> helpers;
  for (const auto &d : b.deposits)
    helpers.push_back(ext::make_shared<DepositRateHelper>(d.second, parse_tenor(d.first), 2, TARGET(), ModifiedFollowing, true,
                                                          Actual360()));
  for (const auto &s : b.swaps)
    helpers.push_back(ext::make_shared<SwapRateHelper>(s.second, parse_tenor(s.first), TARGET(), Annual, ModifiedFollowing,
                                                       bond_dc(), ext::make_shared<Euribor6M>()));
  auto ts = ext::make_shared<PiecewiseYieldCurve<Discount, LogLinear>>(c.today, helpers, act365());
  ts->enableExtrapolation();
  return Curve{ts};
}

Curve bump_curve(const Curve &cv, double shift) {
  auto ts = ext::make_shared<ZeroSpreadedTermStructure>(Handle<YieldTermStructure>(cv.ts),
                                                        Handle<Quote>(ext::make_shared<SimpleQuote>(shift)), Continuous, NoFrequency,
                                                        act365());
  ts->enableExtrapolation();
  return Curve{ts};
}

double curve_discount(const Curve &c, double t) { return c.ts->discount(t, true); }
double curve_zero(const Curve &c, double t) {
  QL_REQUIRE(t > 0, "zero rate needs t > 0");
  return c.ts->zeroRate(t, Continuous, NoFrequency, true);
}
double curve_forward(const Curve &c, double t1, double t2) {
  QL_REQUIRE(t2 > t1 && t1 >= 0, "forward rate needs 0 <= t1 < t2");
  return c.ts->forwardRate(t1, t2, Continuous, NoFrequency, true);
}
void curve_save_csv(const Curve &c, const std::string &path, double tmax, int n) {
  QL_REQUIRE(tmax > 0 && n >= 2 && n <= 1000000, "curve csv needs tmax > 0 and 2..1000000 points");
  std::ofstream f(path);
  QL_REQUIRE(f, "cannot write " << path);
  char b[200];
  for (int i = 0; i < n; i++) {
    double t = tmax * (i + 1) / n;
    std::snprintf(b, sizeof b, "%.10g %.10g %.10g %.10g\n", t, curve_zero(c, t), curve_discount(c, t), curve_forward(c, t - tmax / n / 2, t + tmax / n / 2));
    f << b;
  }
  f.flush();
  QL_REQUIRE(f, "write failed for " << path);
}

// ---------------------------------------------------------------- options
Result price_option(const Clock &c, const Option &o, const Model &m, const Engine &e) {
  activate(c);
  check_model(m);
  QL_REQUIRE(o.strike > 0, "strike must be positive");
  Date expiry = years_ahead(c, o.years);
  ext::shared_ptr<Exercise> ex;
  if (o.american) ex = ext::make_shared<AmericanExercise>(c.today, expiry);
  else ex = ext::make_shared<EuropeanExercise>(expiry);
  VanillaOption opt(ext::make_shared<PlainVanillaPayoff>(o.call ? QuantLib::Option::Call : QuantLib::Option::Put, o.strike), ex);

  bool mc = false;
  if (m.heston) {
    QL_REQUIRE(e.kind == EngineKind::Analytic, "Heston models are priced with the analytic engine only");
    QL_REQUIRE(!o.american, "the Heston analytic engine prices European options only");
    opt.setPricingEngine(ext::make_shared<AnalyticHestonEngine>(ext::make_shared<HestonModel>(heston_process(c, m)), 128));
  } else {
    auto p = bs_process(c, m);
    switch (e.kind) {
      case EngineKind::Analytic:
        QL_REQUIRE(!o.american, "the analytic engine prices European options only; use baw, binomial or finite_diff for American");
        opt.setPricingEngine(ext::make_shared<AnalyticEuropeanEngine>(p));
        break;
      case EngineKind::Binomial: {
        Size n = static_cast<Size>(e.steps);
        const std::string &t = e.tree;
        if (t == "crr") opt.setPricingEngine(tree_engine<CoxRossRubinstein>(p, n));
        else if (t == "jr") opt.setPricingEngine(tree_engine<JarrowRudd>(p, n));
        else if (t == "tian") opt.setPricingEngine(tree_engine<Tian>(p, n));
        else if (t == "lr") opt.setPricingEngine(tree_engine<LeisenReimer>(p, n % 2 ? n : n + 1));
        else QL_FAIL("unknown binomial tree '" << t << "': use crr, jr, tian or lr");
        break;
      }
      case EngineKind::FiniteDifference:
        opt.setPricingEngine(ext::make_shared<FdBlackScholesVanillaEngine>(p, static_cast<Size>(e.steps), static_cast<Size>(e.grid)));
        break;
      case EngineKind::MonteCarlo:
        QL_REQUIRE(!o.american, "the Monte Carlo engine prices European options only");
        // QuantLib's MC statistics keep every sample (a (value, weight) pair). Fail before simulating when
        // the vector cannot fit: at the end it holds >= paths pairs, and growing it copied >= paths/2.
        membudget::require(static_cast<std::size_t>(e.paths) / 2 * 3 * sizeof(std::pair<Real, Real>));
        opt.setPricingEngine(MakeMCEuropeanEngine<PseudoRandom>(p)
                                 .withSteps(1)
                                 .withSamples(static_cast<Size>(e.paths))
                                 .withSeed(static_cast<BigNatural>(e.seed))
                                 .withAntitheticVariate());
        mc = true;
        break;
      case EngineKind::BaroneAdesiWhaley:
        opt.setPricingEngine(ext::make_shared<BaroneAdesiWhaleyApproximationEngine>(p));
        break;
    }
  }
  Result r;
  r.price = opt.NPV();
  if (mc) r.stderr_ = opt.errorEstimate();
  r.delta = greek([&] { return opt.delta(); });
  r.gamma = greek([&] { return opt.gamma(); });
  r.vega = greek([&] { return opt.vega(); });
  r.theta = greek([&] { return opt.theta(); });
  r.rho = greek([&] { return opt.rho(); });
  return r;
}

namespace {
double px(const Clock &c, const Option &o, const Model &m, const Engine &e) { return price_option(c, o, m, e).price; }
void need_bs(const Model &m, const char *who) { QL_REQUIRE(!m.heston, who << " bumps Black-Scholes inputs; it is not available for Heston models"); }
}  // namespace

double bumped_delta(const Clock &c, const Option &o, const Model &m, const Engine &e) {
  double h = 0.01 * m.spot;
  Model up = m, dn = m;
  up.spot += h; dn.spot -= h;
  return (px(c, o, up, e) - px(c, o, dn, e)) / (2 * h);
}
double bumped_gamma(const Clock &c, const Option &o, const Model &m, const Engine &e) {
  double h = 0.01 * m.spot;
  Model up = m, dn = m;
  up.spot += h; dn.spot -= h;
  return (px(c, o, up, e) - 2 * px(c, o, m, e) + px(c, o, dn, e)) / (h * h);
}
double bumped_vega(const Clock &c, const Option &o, const Model &m, const Engine &e) {
  need_bs(m, "vega");
  double h = std::min(0.001, 0.5 * m.vol);  // keep the down bump at a positive volatility
  Model up = m, dn = m;
  up.vol += h; dn.vol -= h;
  return (px(c, o, up, e) - px(c, o, dn, e)) / (2 * h);
}
double bumped_rho(const Clock &c, const Option &o, const Model &m, const Engine &e) {
  double h = 0.0001;
  Model up = m, dn = m;
  up.r += h; dn.r -= h;
  return (px(c, o, up, e) - px(c, o, dn, e)) / (2 * h);
}
double bumped_theta(const Clock &c, const Option &o, const Model &m, const Engine &e) {
  // one calendar day either side of the expiry: theta = -dV/dT per year
  double days = o.years * 365.0, h = 1.0 / 365.0;
  QL_REQUIRE(days > 2, "theta needs more than two days to expiry");
  Option up = o, dn = o;
  up.years += h; dn.years -= h;
  return -(px(c, up, m, e) - px(c, dn, m, e)) / (2 * h);
}
void save_profile(const Clock &c, const Option &o, const Model &m, const Engine &e, double lo, double hi, int n, const std::string &path) {
  QL_REQUIRE(lo > 0 && hi > lo && n >= 2 && n <= 100000, "profile needs 0 < lo < hi and 2..100000 points");
  std::ofstream f(path);
  QL_REQUIRE(f, "cannot write " << path);
  char b[200];
  for (int i = 0; i < n; i++) {
    Model mm = m;
    mm.spot = lo + (hi - lo) * i / (n - 1);
    std::snprintf(b, sizeof b, "%.10g %.10g %.10g %.10g\n", mm.spot, px(c, o, mm, e), bumped_delta(c, o, mm, e),
                  bumped_gamma(c, o, mm, e));
    f << b;
  }
  f.flush();
  QL_REQUIRE(f, "write failed for " << path);
}

// ---------------------------------------------------------------- bonds
namespace {
ext::shared_ptr<QuantLib::Bond> make_bond(const Clock &c, const lmfinance::Bond &b) {
  QL_REQUIRE(b.face > 0, "face value must be positive");
  QL_REQUIRE(b.years > 0 && b.years <= 100, "bond maturity must be between 0 and 100 years");
  int months = static_cast<int>(std::lround(b.years * 12));
  QL_REQUIRE(months >= 1 && std::fabs(b.years * 12 - months) < 1e-9, "bond maturity must be a whole number of months");
  Date maturity = NullCalendar().advance(c.today, Period(months, Months), Unadjusted);
  if (b.zero) return ext::make_shared<ZeroCouponBond>(0, null_cal(), b.face, maturity, Unadjusted, 100.0, c.today);
  Frequency fq = frequency(b.freq);
  QL_REQUIRE(months % (12 / b.freq) == 0, "bond maturity must be a whole number of coupon periods");
  Schedule sch(c.today, maturity, Period(fq), null_cal(), Unadjusted, Unadjusted, DateGeneration::Backward, false);
  return ext::make_shared<FixedRateBond>(0, b.face, sch, std::vector<Rate>{b.coupon}, bond_dc(), Unadjusted, 100.0, c.today);
}
Frequency yield_freq(const lmfinance::Bond &b) { return b.zero ? Annual : frequency(b.freq); }
}  // namespace

double bond_clean_price(const Clock &c, const lmfinance::Bond &b, const Curve &cv) {
  activate(c);
  auto q = make_bond(c, b);
  q->setPricingEngine(ext::make_shared<DiscountingBondEngine>(Handle<YieldTermStructure>(cv.ts)));
  return q->cleanPrice();
}
double bond_npv(const Clock &c, const lmfinance::Bond &b, const Curve &cv) {
  activate(c);
  auto q = make_bond(c, b);
  q->setPricingEngine(ext::make_shared<DiscountingBondEngine>(Handle<YieldTermStructure>(cv.ts)));
  return q->NPV();
}
double bond_accrued(const Clock &c, const lmfinance::Bond &b) { activate(c); return make_bond(c, b)->accruedAmount(); }
double bond_clean_from_yield(const Clock &c, const lmfinance::Bond &b, double y) {
  activate(c);
  return make_bond(c, b)->cleanPrice(y, bond_dc(), Compounded, yield_freq(b));
}
double bond_yield(const Clock &c, const lmfinance::Bond &b, double clean) {
  activate(c);
  return make_bond(c, b)->yield(QuantLib::Bond::Price(clean, QuantLib::Bond::Price::Clean), bond_dc(), Compounded, yield_freq(b));
}
double bond_duration(const Clock &c, const lmfinance::Bond &b, double y, bool macaulay) {
  activate(c);
  return BondFunctions::duration(*make_bond(c, b), InterestRate(y, bond_dc(), Compounded, yield_freq(b)),
                                 macaulay ? Duration::Macaulay : Duration::Modified);
}
double bond_convexity(const Clock &c, const lmfinance::Bond &b, double y) {
  activate(c);
  return BondFunctions::convexity(*make_bond(c, b), InterestRate(y, bond_dc(), Compounded, yield_freq(b)));
}

// ---------------------------------------------------------------- swaps
namespace {
ext::shared_ptr<VanillaSwap> make_swap(const Clock &c, const lmfinance::Swap &s, const Curve &cv) {
  QL_REQUIRE(s.notional > 0, "swap notional must be positive");
  int months = static_cast<int>(std::lround(s.years * 12));
  QL_REQUIRE(months >= 12 && months % 12 == 0 && s.years <= 100, "swap maturity must be a whole number of years, 1 to 100");
  Handle<YieldTermStructure> h(cv.ts);
  auto index = ext::make_shared<Euribor6M>(h);
  auto sw = MakeVanillaSwap(Period(months, Months), index)
                .withFixedRate(s.rate)
                .withNominal(s.notional)
                .withType(s.receiver ? QuantLib::Swap::Receiver : QuantLib::Swap::Payer)
                .withFixedLegTenor(Period(1, Years))
                .withFixedLegDayCount(bond_dc())
                .withFloatingLegSpread(s.spread)
                .withDiscountingTermStructure(h)
                .operator ext::shared_ptr<VanillaSwap>();
  return sw;
}
}  // namespace

double swap_npv(const Clock &c, const lmfinance::Swap &s, const Curve &cv) { activate(c); return make_swap(c, s, cv)->NPV(); }
double swap_par_rate(const Clock &c, const lmfinance::Swap &s, const Curve &cv) { activate(c); return make_swap(c, s, cv)->fairRate(); }
double swap_annuity(const Clock &c, const lmfinance::Swap &s, const Curve &cv) {
  activate(c);
  return std::fabs(make_swap(c, s, cv)->fixedLegBPS()) / 1e-4;
}

// ---------------------------------------------------------------- calibration
double implied_vol(const Clock &c, const Option &o, const Model &m, const Engine &e, double target) {
  need_bs(m, "implied volatility");
  const double lo = 1e-4, hi = 5.0;
  auto f = [&](Real v) { Model mm = m; mm.vol = v; return px(c, o, mm, e) - target; };
  QL_REQUIRE(f(lo) <= 0 && f(hi) >= 0, "price " << target << " is outside the range reachable with volatility in [" << lo << ", " << hi << "]");
  Brent solver;
  solver.setMaxEvaluations(200);
  return solver.solve(f, 1e-10, std::min(std::max(m.vol, 0.01), 4.0), lo, hi);
}

void heston_fit(const Clock &c, HestonFit &h) {
  activate(c);
  QL_REQUIRE(h.spot > 0, "spot must be positive");
  QL_REQUIRE(h.quotes.size() >= 5, "Heston fitting needs at least 5 quotes (it has 5 parameters)");
  Handle<YieldTermStructure> r = flat(c, h.r), q = flat(c, h.q);
  auto process = ext::make_shared<HestonProcess>(r, q, Handle<Quote>(ext::make_shared<SimpleQuote>(h.spot)), h.v0, h.kappa, h.theta,
                                                 h.sigma, h.rho);
  auto model = ext::make_shared<HestonModel>(process);
  auto engine = ext::make_shared<AnalyticHestonEngine>(model, 64);
  std::vector<ext::shared_ptr<CalibrationHelper>> helpers;
  for (const auto &qt : h.quotes) {
    QL_REQUIRE(qt.strike > 0 && qt.vol > 0 && qt.years > 0, "quote() needs positive strike, years and volatility");
    int days = static_cast<int>(std::lround(qt.years * 365.0));
    auto helper = ext::make_shared<HestonModelHelper>(Period(days, Days), null_cal(), h.spot, qt.strike,
                                                      Handle<Quote>(ext::make_shared<SimpleQuote>(qt.vol)), r, q,
                                                      BlackCalibrationHelper::ImpliedVolError);
    helper->setPricingEngine(engine);
    helpers.push_back(helper);
  }
  LevenbergMarquardt om(1e-10, 1e-10, 1e-10);
  model->calibrate(helpers, om, EndCriteria(800, 100, 1e-10, 1e-10, 1e-10));
  h.v0 = model->v0();
  h.kappa = model->kappa();
  h.theta = model->theta();
  h.sigma = model->sigma();
  h.rho = model->rho();
  double sse = 0;
  for (const auto &x : helpers) { double e = x->calibrationError(); sse += e * e; }
  h.error = std::sqrt(sse / helpers.size());
}

double normal_quantile(double p) {
  QL_REQUIRE(p > 0 && p < 1, "probability must be inside (0, 1)");
  return InverseCumulativeNormal()(p);
}
double normal_density(double x) { return NormalDistribution()(x); }

}  // namespace lmfinance
