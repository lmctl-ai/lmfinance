#include "finance.hpp"
#include "memory_budget.hpp"

#include <ql/cashflows/cashflows.hpp>
#include <ql/exercise.hpp>
#include <ql/indexes/ibor/euribor.hpp>
#include <ql/indexes/swap/euriborswap.hpp>
#include <ql/instruments/asianoption.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/instruments/bonds/fixedratebond.hpp>
#include <ql/instruments/bonds/zerocouponbond.hpp>
#include <ql/instruments/capfloor.hpp>
#include <ql/instruments/makecapfloor.hpp>
#include <ql/instruments/makeswaption.hpp>
#include <ql/instruments/makevanillaswap.hpp>
#include <ql/instruments/swaption.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <ql/instruments/vanillaswap.hpp>
#include <ql/pricingengines/asian/analytic_discr_geom_av_price.hpp>
#include <ql/pricingengines/asian/mc_discr_arith_av_price.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
#include <ql/pricingengines/capfloor/blackcapfloorengine.hpp>
#include <ql/pricingengines/swaption/blackswaptionengine.hpp>
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
#include <ql/termstructures/yield/discountcurve.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/termstructures/yield/piecewiseyieldcurve.hpp>
#include <ql/termstructures/yield/bondhelpers.hpp>
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
#include <cstdlib>
#include <fstream>
#include <sstream>

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
  if (b.discount) {
    QL_REQUIRE(!b.discounts.empty(), "the curve needs at least one discount() node");
    auto nodes = b.discounts;
    std::sort(nodes.begin(), nodes.end());
    std::vector<Date> dates{c.today};
    std::vector<DiscountFactor> factors{1.0};
    for (const auto &n : nodes) {
      Date d = years_ahead(c, n.first);
      QL_REQUIRE(d > dates.back(), "discount() nodes must be at distinct maturities (at least one day apart)");
      QL_REQUIRE(n.second > 0, "a discount factor must be positive, got " << n.second);
      dates.push_back(d);
      factors.push_back(n.second);
    }
    auto ts = ext::make_shared<InterpolatedDiscountCurve<LogLinear>>(dates, factors, act365(), null_cal());
    ts->enableExtrapolation();
    return Curve{ts};
  }
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
  QL_REQUIRE(!b.deposits.empty() || !b.swaps.empty() || !b.bonds.empty(), "the curve needs at least one deposit(), swap() or bond() quote");
  std::vector<ext::shared_ptr<RateHelper>> helpers;
  for (const auto &d : b.deposits)
    helpers.push_back(ext::make_shared<DepositRateHelper>(d.second, parse_tenor(d.first), 2, TARGET(), ModifiedFollowing, true,
                                                          Actual360()));
  for (const auto &s : b.swaps)
    helpers.push_back(ext::make_shared<SwapRateHelper>(s.second, parse_tenor(s.first), TARGET(), Annual, ModifiedFollowing,
                                                       bond_dc(), ext::make_shared<Euribor6M>()));
  for (const auto &q : b.bonds) {
    const int months = static_cast<int>(std::lround(q.years * 12));
    QL_REQUIRE(months >= 12 && months % 12 == 0 && q.years <= 100, "bond() maturity must be a whole number of years, 1 to 100");
    QL_REQUIRE(q.clean > 0, "bond() clean price must be positive");
    Schedule sch(c.today, NullCalendar().advance(c.today, Period(months, Months), Unadjusted), Period(Annual), null_cal(), Unadjusted,
                 Unadjusted, DateGeneration::Backward, false);
    helpers.push_back(ext::make_shared<FixedRateBondHelper>(Handle<Quote>(ext::make_shared<SimpleQuote>(q.clean)), 0, 100.0, sch,
                                                            std::vector<Rate>{q.coupon}, bond_dc(), Unadjusted, 100.0, c.today, null_cal()));
  }
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
  QL_REQUIRE(o.exotic != Exotic::Forward, "a forward contract has no option price: use pricing.forward_price, or Option.payoff for its payoff at delivery");
  Date expiry = years_ahead(c, o.years);
  ext::shared_ptr<Exercise> ex;
  if (o.american) ex = ext::make_shared<AmericanExercise>(c.today, expiry);
  else ex = ext::make_shared<EuropeanExercise>(expiry);
  auto payoff = ext::make_shared<PlainVanillaPayoff>(o.call ? QuantLib::Option::Call : QuantLib::Option::Put, o.strike);

  if (o.exotic == Exotic::Barrier) {
    QL_REQUIRE(!m.heston && !o.american, "barrier options are priced by the Black-Scholes analytic engine (European only)");
    QL_REQUIRE(e.kind == EngineKind::Analytic, "barrier options are priced with the analytic engine only");
    Barrier::Type bt = o.barrier_type == "up-in" ? Barrier::UpIn : o.barrier_type == "up-out" ? Barrier::UpOut
                       : o.barrier_type == "down-in" ? Barrier::DownIn : Barrier::DownOut;
    BarrierOption opt(bt, o.barrier, o.rebate, payoff, ex);
    opt.setPricingEngine(ext::make_shared<AnalyticBarrierEngine>(bs_process(c, m)));
    Result r;
    r.price = opt.NPV();
    return r;
  }
  if (o.exotic == Exotic::Asian) {
    QL_REQUIRE(!m.heston && !o.american, "Asian options are priced on a Black-Scholes model (European exercise)");
    std::vector<Date> fixings;
    for (int k = 1; k <= o.fixings; k++) fixings.push_back(c.today + static_cast<Integer>(std::llround(o.years * 365.0 * k / o.fixings)));
    for (std::size_t k = 0; k < fixings.size(); k++)
      QL_REQUIRE(fixings[k] > (k ? fixings[k - 1] : c.today), "Asian fixings fall on the same day: use fewer fixings or a longer maturity");
    Average::Type avg = o.averaging == "geometric" ? Average::Geometric : Average::Arithmetic;
    DiscreteAveragingAsianOption opt(avg, fixings, payoff, ex);
    auto p = bs_process(c, m);
    Result r;
    if (avg == Average::Geometric) {
      QL_REQUIRE(e.kind == EngineKind::Analytic, "geometric Asian options are priced with the analytic engine");
      opt.setPricingEngine(ext::make_shared<AnalyticDiscreteGeometricAveragePriceAsianEngine>(p));
      r.price = opt.NPV();
    } else {
      QL_REQUIRE(e.kind == EngineKind::MonteCarlo, "arithmetic Asian options are priced with the monte_carlo engine (geometric control variate)");
      membudget::require(static_cast<std::size_t>(e.paths) / 2 * 3 * sizeof(std::pair<Real, Real>));
      opt.setPricingEngine(MakeMCDiscreteArithmeticAPEngine<PseudoRandom>(p)
                               .withSamples(static_cast<Size>(e.paths))
                               .withSeed(static_cast<BigNatural>(e.seed))
                               .withAntitheticVariate()
                               .withControlVariate());
      r.price = opt.NPV();
      r.stderr_ = opt.errorEstimate();
    }
    return r;
  }
  VanillaOption opt(payoff, ex);

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
  if (b.dated) {
    Date issue = parse_date(b.issue), mat = parse_date(b.maturity);
    QL_REQUIRE(mat > issue, "bond maturity " << b.maturity << " must be after the issue date " << b.issue);
    QL_REQUIRE(mat > c.today, "the bond has matured: its maturity " << b.maturity << " is not after the evaluation date " << format_date(c.today));
    Schedule sch(issue, mat, Period(frequency(b.freq)), null_cal(), Unadjusted, Unadjusted, DateGeneration::Backward, false);
    return ext::make_shared<FixedRateBond>(0, b.face, sch, std::vector<Rate>{b.coupon}, bond_dc(), Unadjusted, 100.0, issue);
  }
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

// A swap from instrument.swap_simple: textbook conventions, computed from the curve's discount factors at exact times
// k / freq (no dates, no calendars, no fixing lag). Both legs pay every 1/freq years with accrual exactly 1/freq; the
// floating rate over a period is the curve's simple forward, so the period's floating flow is N (D(t-1/f) / D(t) - 1).
namespace {
int simple_periods(const lmfinance::Swap &s) {
  QL_REQUIRE(s.notional > 0, "swap notional must be positive");
  QL_REQUIRE(s.freq == 1 || s.freq == 2 || s.freq == 4 || s.freq == 12, "swap payments per year must be 1, 2, 4 or 12, got " << s.freq);
  const double n = s.years * s.freq;
  const int p = static_cast<int>(std::lround(n));
  QL_REQUIRE(p >= 1 && std::fabs(n - p) < 1e-9 && s.years <= 100, "swap length must be a whole number of payment periods, up to 100 years");
  return p;
}
double simple_annuity(const lmfinance::Swap &s, const Curve &cv) {  // N sum D(t_k) / freq
  double a = 0;
  const int p = simple_periods(s);
  for (int k = 1; k <= p; k++) a += curve_discount(cv, static_cast<double>(k) / s.freq);
  return s.notional * a / s.freq;
}
double simple_fixed(const lmfinance::Swap &s, const Curve &cv) { return s.rate * simple_annuity(s, cv); }
double simple_float(const lmfinance::Swap &s, const Curve &cv) {
  const int p = simple_periods(s);
  return s.notional * (1.0 - curve_discount(cv, static_cast<double>(p) / s.freq)) + s.spread * simple_annuity(s, cv);
}
}  // namespace

double swap_npv(const Clock &c, const lmfinance::Swap &s, const Curve &cv) {
  activate(c);
  if (s.simple) { const double v = simple_float(s, cv) - simple_fixed(s, cv); return s.receiver ? -v : v; }
  return make_swap(c, s, cv)->NPV();
}
double swap_par_rate(const Clock &c, const lmfinance::Swap &s, const Curve &cv) {
  activate(c);
  if (s.simple) return simple_float(s, cv) / simple_annuity(s, cv);
  return make_swap(c, s, cv)->fairRate();
}
double swap_annuity(const Clock &c, const lmfinance::Swap &s, const Curve &cv) {
  activate(c);
  if (s.simple) return simple_annuity(s, cv);
  return std::fabs(make_swap(c, s, cv)->fixedLegBPS()) / 1e-4;
}
// 13. Leg values (positive magnitudes) and cash flows. Market swaps use QuantLib's legs; "net" is for simple swaps.
double swap_fixed_leg_npv(const Clock &c, const lmfinance::Swap &s, const Curve &cv) {
  activate(c);
  return s.simple ? simple_fixed(s, cv) : std::fabs(make_swap(c, s, cv)->fixedLegNPV());
}
double swap_float_leg_npv(const Clock &c, const lmfinance::Swap &s, const Curve &cv) {
  activate(c);
  return s.simple ? simple_float(s, cv) : std::fabs(make_swap(c, s, cv)->floatingLegNPV());
}
std::vector<double> swap_flows(const Clock &c, const lmfinance::Swap &s, const Curve &cv, const std::string &leg) {
  activate(c);
  QL_REQUIRE(leg == "fixed" || leg == "float" || leg == "net", "swap leg must be fixed, float or net, got '" << leg << "'");
  std::vector<double> out;
  if (!s.simple) {
    QL_REQUIRE(leg != "net", "net flows need both legs on the same dates: use instrument.swap_simple");
    auto sw = make_swap(c, s, cv);
    const Leg &lg = leg == "fixed" ? sw->fixedLeg() : sw->floatingLeg();   // sw keeps the leg alive
    for (const auto &cf : lg) out.push_back(std::fabs(cf->amount()));
    return out;
  }
  const int p = simple_periods(s);
  for (int k = 1; k <= p; k++) {
    const double d0 = curve_discount(cv, static_cast<double>(k - 1) / s.freq), d1 = curve_discount(cv, static_cast<double>(k) / s.freq);
    const double fixed = s.notional * s.rate / s.freq;
    const double flt = s.notional * (d0 / d1 - 1.0) + s.notional * s.spread / s.freq;
    const double net = s.receiver ? fixed - flt : flt - fixed;   // the holder's cash: payer receives float, pays fixed
    out.push_back(leg == "fixed" ? fixed : leg == "float" ? flt : net);
  }
  return out;
}

// ---------------------------------------------------------------- caps, floors, swaptions (Black)
namespace {
ext::shared_ptr<QuantLib::CapFloor> make_capfloor(const Clock &c, const lmfinance::CapFloor &cf, const Curve &cv) {
  QL_REQUIRE(cf.notional > 0, "cap/floor notional must be positive");
  QL_REQUIRE(cf.strike > 0, "cap/floor strike must be positive");
  int months = static_cast<int>(std::lround(cf.years * 12));
  QL_REQUIRE(months >= 12 && months % 6 == 0 && cf.years <= 100, "cap/floor maturity must be a whole number of half years, 1 to 100 years");
  Handle<YieldTermStructure> h(cv.ts);
  auto index = ext::make_shared<Euribor6M>(h);
  return MakeCapFloor(cf.cap ? QuantLib::CapFloor::Cap : QuantLib::CapFloor::Floor, Period(months, Months), index)
      .withStrike(cf.strike)
      .withNominal(cf.notional)
      .operator ext::shared_ptr<QuantLib::CapFloor>();
}
ext::shared_ptr<QuantLib::Swaption> make_swaption(const Clock &c, const lmfinance::Swaption &sw, const Curve &cv) {
  QL_REQUIRE(sw.swap.notional > 0, "swap notional must be positive");
  int months = static_cast<int>(std::lround(sw.swap.years * 12));
  QL_REQUIRE(months >= 12 && months % 12 == 0 && sw.swap.years <= 100, "swap maturity must be a whole number of years, 1 to 100");
  int emonths = static_cast<int>(std::lround(sw.expiry * 12));
  QL_REQUIRE(emonths >= 1 && std::fabs(sw.expiry * 12 - emonths) < 1e-9 && sw.expiry <= 50, "swaption expiry must be a whole number of months, up to 50 years");
  Handle<YieldTermStructure> h(cv.ts);
  auto index = ext::make_shared<EuriborSwapIsdaFixA>(Period(months, Months), h);
  (void)c;
  return MakeSwaption(index, Period(emonths, Months), sw.swap.rate)
      .withNominal(sw.swap.notional)
      .withUnderlyingType(sw.swap.receiver ? QuantLib::Swap::Receiver : QuantLib::Swap::Payer)
      .operator ext::shared_ptr<QuantLib::Swaption>();
}
}  // namespace

double capfloor_npv(const Clock &c, const lmfinance::CapFloor &cf, const Curve &cv, double vol) {
  activate(c);
  QL_REQUIRE(vol > 0, "cap/floor volatility must be positive");
  auto q = make_capfloor(c, cf, cv);
  q->setPricingEngine(ext::make_shared<BlackCapFloorEngine>(Handle<YieldTermStructure>(cv.ts), vol));
  return q->NPV();
}
double capfloor_implied_vol(const Clock &c, const lmfinance::CapFloor &cf, const Curve &cv, double price) {
  activate(c);
  QL_REQUIRE(price > 0, "cap/floor price must be positive");
  auto q = make_capfloor(c, cf, cv);
  return q->impliedVolatility(price, Handle<YieldTermStructure>(cv.ts), 0.2, 1e-8, 200, 1e-5, 4.0);
}
double capfloor_atm_rate(const Clock &c, const lmfinance::CapFloor &cf, const Curve &cv) {
  activate(c);
  return make_capfloor(c, cf, cv)->atmRate(*cv.ts);
}
double swaption_npv(const Clock &c, const lmfinance::Swaption &sw, const Curve &cv, double vol) {
  activate(c);
  QL_REQUIRE(vol > 0, "swaption volatility must be positive");
  auto q = make_swaption(c, sw, cv);
  q->setPricingEngine(ext::make_shared<BlackSwaptionEngine>(Handle<YieldTermStructure>(cv.ts), vol));
  return q->NPV();
}
double swaption_implied_vol(const Clock &c, const lmfinance::Swaption &sw, const Curve &cv, double price) {
  activate(c);
  QL_REQUIRE(price > 0, "swaption price must be positive");
  auto q = make_swaption(c, sw, cv);
  return q->impliedVolatility(price, Handle<YieldTermStructure>(cv.ts), 0.2, 1e-8, 200, 1e-5, 4.0);
}
double swaption_forward_rate(const Clock &c, const lmfinance::Swaption &sw, const Curve &cv) {
  activate(c);
  auto q = make_swaption(c, sw, cv);
  q->setPricingEngine(ext::make_shared<BlackSwaptionEngine>(Handle<YieldTermStructure>(cv.ts), 0.2));
  return q->underlying()->fairRate();
}

// ---------------------------------------------------------------- sample series
Series load_series(const std::string &path) {
  std::ifstream f(path);
  QL_REQUIRE(f, "cannot open " << path);
  Series s;
  std::string line;
  bool first = true;
  while (std::getline(f, line)) {
    std::vector<double> row;
    bool bad = false;
    std::string tok;
    auto flush = [&] {
      if (tok.empty()) return;
      char *end = nullptr;
      double v = std::strtod(tok.c_str(), &end);
      if (!end || *end != '\0' || !std::isfinite(v)) bad = true;
      row.push_back(v);
      tok.clear();
    };
    for (char ch : line) {
      if (ch == ',' || ch == ' ' || ch == '\t' || ch == '\r' || ch == ';') flush();
      else tok += ch;
    }
    flush();
    if (bad) {
      QL_REQUIRE(first, "series " << path << ": line '" << line << "' is not numbers");  // one header line is allowed
    } else {
      s.v.insert(s.v.end(), row.begin(), row.end());
    }
    first = false;
    QL_REQUIRE(s.v.size() <= 100000000, "series " << path << ": at most 1e8 values");
  }
  QL_REQUIRE(!s.v.empty(), "series " << path << " holds no numbers");
  membudget::charge(s.v.size() * sizeof(double));
  return s;
}
double series_quantile(const Series &s, double p) {
  QL_REQUIRE(p >= 0 && p <= 1, "quantile probability must be inside [0, 1]");
  std::vector<double> v = s.v;
  std::sort(v.begin(), v.end());
  double pos = p * (v.size() - 1);
  std::size_t lo = static_cast<std::size_t>(std::floor(pos));
  std::size_t hi = std::min(lo + 1, v.size() - 1);
  return v[lo] + (pos - lo) * (v[hi] - v[lo]);
}
double series_var(const Series &s, double confidence) {
  QL_REQUIRE(confidence > 0.5 && confidence < 1, "confidence must be inside (0.5, 1)");
  return -series_quantile(s, 1 - confidence);
}
double series_es(const Series &s, double confidence) {
  QL_REQUIRE(confidence > 0.5 && confidence < 1, "confidence must be inside (0.5, 1)");
  double q = series_quantile(s, 1 - confidence);
  double sum = 0;
  std::size_t n = 0;
  for (double x : s.v) if (x <= q) { sum += x; n++; }
  return n ? -sum / n : -q;
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
  // Levenberg-Marquardt reports why it stopped; only a stationary point/value is a fit. Hitting the iteration cap or
  // stopping for no reason used to return the last iterate as if it had converged.
  const EndCriteria::Type why = model->endCriteria();
  if (!EndCriteria::succeeded(why) && why != EndCriteria::ZeroGradientNorm && why != EndCriteria::FunctionEpsilonTooSmall) {
    std::ostringstream m;
    m << "Heston calibration did not converge (" << why << "): try another guess() or more quotes";
    QL_FAIL(m.str());
  }
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

// ---------------------------------------------------------------- finance-track additions
namespace {
struct Compound { Compounding c; Frequency f; };
Compound parse_compounding(const std::string &s) {
  if (s == "simple") return {Simple, NoFrequency};
  if (s == "annual") return {Compounded, Annual};
  if (s == "semiannual") return {Compounded, Semiannual};
  if (s == "quarterly") return {Compounded, Quarterly};
  if (s == "monthly") return {Compounded, Monthly};
  if (s == "continuous") return {Continuous, NoFrequency};
  QL_FAIL("compounding must be simple, annual, semiannual, quarterly, monthly or continuous, got '" << s << "'");
}
void need_finite(double x, const char *what) { QL_REQUIRE(std::isfinite(x), what << " must be finite"); }
}  // namespace

// 1. Number helpers. The rounding works on the decimal value of x at 15 significant digits (what str() prints), so a
// binary value just under a half (1.005 is stored as 1.00499999999999989...) rounds as the decimal 1.005 does, and
// 999.9999999999997 is 1000 to the cent. Half goes away from zero (ClosestRounding's rule, without its binary artefacts).
double round_decimal(double x, int digits) {
  QL_REQUIRE(std::isfinite(x), "lmf.round: value must be finite");
  QL_REQUIRE(digits >= 0 && digits <= 12, "lmf.round: digits must be 0..12, got " << digits);
  if (x == 0.0) return 0.0;
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.14e", std::fabs(x));       // d.dddddddddddddde+XX: 15 significant digits
  std::string mant;
  for (const char *p = buf; *p && *p != 'e'; p++) if (*p != '.') mant += *p;
  const int e10 = std::atoi(std::strchr(buf, 'e') + 1);
  const int keep = e10 + 1 + digits;                           // significant digits that stay
  if (keep >= 15) return x;                                    // already finer than the value's 15 digits
  if (keep < 0) return 0.0;
  long long n = 0;
  for (int i = 0; i < keep; i++) n = n * 10 + (mant[i] - '0');
  if (mant[keep] >= '5') n++;
  std::string t = std::to_string(n) + "e-" + std::to_string(digits);
  const double v = std::strtod(t.c_str(), nullptr);
  return std::signbit(x) ? -v : v;
}
std::string money_text(double x) {
  QL_REQUIRE(std::isfinite(x), "lmf.money: value must be finite");
  const double r = round_decimal(x, 2);
  char b[64];
  std::snprintf(b, sizeof b, "%.2f", std::fabs(r));
  std::string s = b, ip = s.substr(0, s.size() - 3), fp = s.substr(s.size() - 3), grouped;
  for (std::size_t i = 0; i < ip.size(); i++) {
    if (i && (ip.size() - i) % 3 == 0) grouped += ',';
    grouped += ip[i];
  }
  return (r < 0 ? "-" : "") + grouped + fp;
}
// Closest fraction with a denominator up to the bound, by continued fractions: the expansion stops at the first
// convergent that equals x to 12 digits, or at the last convergent whose denominator fits. Integers print as "3".
std::string fraction_text(double x, int64_t max_den) {
  QL_REQUIRE(std::isfinite(x) && std::fabs(x) < 1e12, "lmf.fraction: value must be finite and below 1e12 in size");
  QL_REQUIRE(max_den >= 1 && max_den <= 1000000000, "lmf.fraction: max_denominator must be 1..1000000000");
  const bool neg = x < 0;
  const double ax = std::fabs(x);
  long long p0 = 0, q0 = 1, p1 = 1, q1 = 0;                    // convergents p_{k-2}/q_{k-2}, p_{k-1}/q_{k-1}
  double r = ax;
  long long bp = 0, bq = 1;
  for (int k = 0; k < 64; k++) {
    const double fl = std::floor(r);
    const long long a = static_cast<long long>(fl);
    const long long p2 = a * p1 + p0, q2 = a * q1 + q0;
    if (q2 > max_den) break;
    p0 = p1; q0 = q1; p1 = p2; q1 = q2;
    bp = p2; bq = q2;
    if (std::fabs(ax - static_cast<double>(bp) / static_cast<double>(bq)) <= 1e-12 * std::max(1.0, ax)) break;
    const double frac = r - fl;
    if (frac < 1e-15) break;
    r = 1.0 / frac;
  }
  std::string s = (neg && bp != 0 ? "-" : "") + std::to_string(bp);
  if (bq != 1) s += "/" + std::to_string(bq);
  return s;
}
double math_exp(double x) {
  need_finite(x, "lmf.exp argument");
  const double v = std::exp(x);
  QL_REQUIRE(std::isfinite(v), "lmf.exp: result is too large");
  return v;
}
double math_log(double x) {
  need_finite(x, "lmf.log argument");
  QL_REQUIRE(x > 0, "lmf.log: argument must be positive, got " << x);
  return std::log(x);
}

// 4. A flat curve with a stated compounding. discount(t) is (1 + r/m)^(-m t), 1/(1 + r t) for simple, e^(-r t) for
// continuous; t is in years of 365 days, exactly (Curve.discount takes t, not a date).
Curve flat_curve_compounded(const Clock &c, double rate, const std::string &compounding) {
  activate(c);
  const Compound k = parse_compounding(compounding);
  QL_REQUIRE(rate > -1.0, "rate must be above -100%");
  auto ts = ext::make_shared<FlatForward>(c.today, rate, act365(), k.c, k.f);
  ts->enableExtrapolation();
  return Curve{ts};
}
// The equivalent rate over one year (for simple compounding that is the rate whose one-year growth factor matches).
double equivalent_rate(double rate, const std::string &from, const std::string &to) {
  const Compound a = parse_compounding(from), b = parse_compounding(to);
  QL_REQUIRE(rate > -1.0, "rate must be above -100%");
  return InterestRate(rate, act365(), a.c, a.f).equivalentRate(b.c, b.f, 1.0).rate();
}

// 7. Present value of dated-in-years cash flows: sum of amount * D(t), t = 0 included (a flow today is not discounted).
double npv(const Curve &cv, const std::vector<double> &t, const std::vector<double> &a) {
  QL_REQUIRE(t.size() == a.size() && !t.empty(), "npv needs equally long, non-empty lists of times and amounts (got " << t.size() << " and " << a.size() << ")");
  double s = 0;
  for (std::size_t i = 0; i < t.size(); i++) {
    QL_REQUIRE(std::isfinite(t[i]) && std::isfinite(a[i]) && t[i] >= 0, "npv: times must be finite and not negative, amounts finite");
    s += a[i] * curve_discount(cv, t[i]);
  }
  return s;
}
std::vector<double> pv_terms(const Curve &cv, const std::vector<double> &t, const std::vector<double> &a) {
  QL_REQUIRE(t.size() == a.size() && !t.empty(), "pv_terms needs equally long, non-empty lists of times and amounts (got " << t.size() << " and " << a.size() << ")");
  std::vector<double> out;
  for (std::size_t i = 0; i < t.size(); i++) {
    QL_REQUIRE(std::isfinite(t[i]) && std::isfinite(a[i]) && t[i] >= 0, "pv_terms: times must be finite and not negative, amounts finite");
    out.push_back(a[i] * curve_discount(cv, t[i]));
  }
  return out;
}

// 17. Ordinary annuities: payments at the end of each of n periods, rate per period.
double annuity_pv(double pay, double r, int64_t n) {
  QL_REQUIRE(n >= 1 && n <= 100000, "annuity: periods must be 1..100000");
  QL_REQUIRE(std::isfinite(pay) && std::isfinite(r) && r > -1.0, "annuity: payment must be finite and rate above -100%");
  if (r == 0.0) return pay * static_cast<double>(n);
  return pay * (1.0 - std::pow(1.0 + r, -static_cast<double>(n))) / r;
}
double annuity_fv(double pay, double r, int64_t n) {
  QL_REQUIRE(n >= 1 && n <= 100000, "annuity: periods must be 1..100000");
  QL_REQUIRE(std::isfinite(pay) && std::isfinite(r) && r > -1.0, "annuity: payment must be finite and rate above -100%");
  if (r == 0.0) return pay * static_cast<double>(n);
  return pay * (std::pow(1.0 + r, static_cast<double>(n)) - 1.0) / r;
}

// 20. Level-payment loan. The schedule works in whole cents: payment rounded once, interest = round(balance * rate)
// each period, the last payment takes the residual so the balance ends at exactly 0.
double loan_payment(double p, double r, int64_t n) {
  QL_REQUIRE(p > 0 && std::isfinite(p), "loan: principal must be positive");
  const double factor = annuity_pv(1.0, r, n);
  QL_REQUIRE(factor > 0, "loan: rate and periods give no payment");
  return p / factor;
}
std::vector<double> amortization(double p, double r, int64_t n, const std::string &column) {
  QL_REQUIRE(column == "payment" || column == "interest" || column == "principal" || column == "balance",
             "amortization column must be payment, interest, principal or balance, got '" << column << "'");
  QL_REQUIRE(r >= 0, "amortization: rate must not be negative");
  const long long pay = std::llround(round_decimal(loan_payment(p, r, n), 2) * 100.0);
  long long bal = std::llround(round_decimal(p, 2) * 100.0);
  std::vector<double> out;
  for (int64_t k = 1; k <= n; k++) {
    const long long interest = std::llround(round_decimal(static_cast<double>(bal) / 100.0 * r, 2) * 100.0);
    long long pmt = pay, prin = pay - interest;
    if (k == n) { prin = bal; pmt = bal + interest; }
    bal -= prin;
    const long long v = column == "payment" ? pmt : column == "interest" ? interest : column == "principal" ? prin : bal;
    out.push_back(static_cast<double>(v) / 100.0);
  }
  return out;
}

// 14. Zero and forward rates with a stated compounding (the existing zero and forward stay continuous).
double curve_zero_rate(const Curve &c, double t, const std::string &compounding) {
  QL_REQUIRE(t > 0, "zero rate needs t > 0");
  const Compound k = parse_compounding(compounding);
  return c.ts->zeroRate(t, k.c, k.f, true).rate();
}
double curve_forward_rate(const Curve &c, double t1, double t2, const std::string &compounding) {
  QL_REQUIRE(t2 > t1 && t1 >= 0, "forward rate needs 0 <= t1 < t2");
  const Compound k = parse_compounding(compounding);
  return c.ts->forwardRate(t1, t2, k.c, k.f, true).rate();
}

// 8. Payoff at expiry. A forward's payoff is S - K (K the delivery price); plain options max(S-K, 0) / max(K-S, 0).
double option_payoff(const Option &o, double s) {
  QL_REQUIRE(std::isfinite(s), "spot must be finite");
  QL_REQUIRE(o.exotic == Exotic::None || o.exotic == Exotic::Forward,
             "payoff at expiry is defined here for plain options and forwards; barrier and Asian payoffs depend on the path");
  if (o.exotic == Exotic::Forward) return s - o.strike;
  return o.call ? std::max(s - o.strike, 0.0) : std::max(o.strike - s, 0.0);
}
// 10. Strategy: sum of quantity * payoff of each leg.
double strategy_payoff(const Strategy &st, double s) {
  QL_REQUIRE(!st.legs.empty(), "the strategy has no legs: add(option, quantity) first");
  double v = 0;
  for (const auto &l : st.legs) v += l.second * option_payoff(l.first, s);
  return v;
}
std::vector<double> strategy_payoffs(const Strategy &st, double lo, double hi, int points) {
  QL_REQUIRE(points >= 2 && points <= 100000, "payoffs: points must be 2..100000");
  QL_REQUIRE(std::isfinite(lo) && std::isfinite(hi) && hi > lo, "payoffs: need lo < hi");
  std::vector<double> out;
  for (int i = 0; i < points; i++) out.push_back(strategy_payoff(st, lo + (hi - lo) * i / (points - 1)));
  return out;
}

// 15. Perpetuity whose first payment is one period away and equals cash * (1 + growth).
double perpetuity(double cash, double rate, double growth) {
  QL_REQUIRE(std::isfinite(cash) && std::isfinite(rate) && std::isfinite(growth), "perpetuity: arguments must be finite");
  QL_REQUIRE(rate > growth, "perpetuity: the rate (" << rate << ") must exceed the growth (" << growth << ")");
  return cash * (1.0 + growth) / (rate - growth);
}
// Flows at t = 1..n discounted at `rate` per period, plus a growing perpetuity after the last flow:
// the flow n+1 is flows[n-1] * (1 + g), valued at time n as flows[n-1] (1 + g) / (r - g).
double dcf_two_stage(double rate, const std::vector<double> &flows, double g) {
  QL_REQUIRE(!flows.empty(), "dcf_two_stage needs at least one explicit flow");
  QL_REQUIRE(rate > g && rate > -1.0, "dcf_two_stage: the rate must exceed the terminal growth");
  double pv = 0;
  for (std::size_t i = 0; i < flows.size(); i++) {
    QL_REQUIRE(std::isfinite(flows[i]), "dcf_two_stage: flows must be finite");
    pv += flows[i] / std::pow(1.0 + rate, static_cast<double>(i + 1));
  }
  const double n = static_cast<double>(flows.size());
  return pv + flows.back() * (1.0 + g) / (rate - g) / std::pow(1.0 + rate, n);
}

// 16. Cost of carry: F = S / D(T) - income + cost, income and cost valued at delivery.
double forward_price(const Curve &cv, double spot, double years, double income, double cost) {
  QL_REQUIRE(spot > 0 && years > 0, "forward_price: spot and years must be positive");
  QL_REQUIRE(std::isfinite(income) && std::isfinite(cost), "forward_price: income and cost must be finite");
  return spot / curve_discount(cv, years) - income + cost;
}
// Daily cash per unit for a long position: the entry price, then each settlement, as successive differences.
std::vector<double> mark_to_market(double entry, const std::vector<double> &settles) {
  QL_REQUIRE(!settles.empty(), "mark_to_market needs at least one settlement price");
  std::vector<double> out;
  double prev = entry;
  for (double s : settles) {
    QL_REQUIRE(std::isfinite(s) && std::isfinite(entry), "mark_to_market: prices must be finite");
    out.push_back(s - prev);
    prev = s;
  }
  return out;
}

// 2. Explicit binomial tree with a SIMPLE rate per step: q = (1 + r - d) / (u - d).
namespace {
void tree_range(const Tree &t, int step, int ups) {
  QL_REQUIRE(step >= 0 && step <= t.steps, "tree step " << step << " is outside 0.." << t.steps);
  QL_REQUIRE(ups >= 0 && ups <= step, "ups " << ups << " is outside 0.." << step << " at step " << step);
}
}  // namespace
double tree_q(const Tree &t) { return (1.0 + t.rate - t.down) / (t.up - t.down); }
double tree_stock(const Tree &t, int step, int ups) {
  tree_range(t, step, ups);
  return t.spot * std::pow(t.up, ups) * std::pow(t.down, step - ups);
}
double tree_prob(const Tree &t, int step, int ups) {
  tree_range(t, step, ups);
  const double q = tree_q(t);
  const double lc = std::lgamma(step + 1.0) - std::lgamma(ups + 1.0) - std::lgamma(step - ups + 1.0);
  return std::exp(lc) * std::pow(q, ups) * std::pow(1.0 - q, step - ups);
}
double tree_state_price(const Tree &t, int step, int ups) { return tree_prob(t, step, ups) / std::pow(1.0 + t.rate, step); }
bool tree_arbitrage_free(const Tree &t) { return t.down < 1.0 + t.rate && 1.0 + t.rate < t.up; }

// 3. Backward induction on the explicit tree. Node (i, j): i steps, j ups; children (i+1, j+1) up and (i+1, j) down.
TreeResult tree_price(const Option &o, const Tree &t) {
  QL_REQUIRE(o.exotic == Exotic::None, "tree_price prices plain calls and puts, European or American");
  QL_REQUIRE(tree_arbitrage_free(t), "the tree is not arbitrage-free: need down < 1 + rate < up (down " << t.down << ", 1 + rate "
                                      << 1.0 + t.rate << ", up " << t.up << ")");
  TreeResult r;
  r.call = o.call; r.american = o.american; r.strike = o.strike; r.steps = t.steps; r.rate = t.rate; r.q = tree_q(t);
  const int n = t.steps;
  r.stock.resize(n + 1); r.value.resize(n + 1); r.intrinsic.resize(n + 1); r.continuation.resize(n + 1); r.exercised.resize(n + 1);
  for (int i = 0; i <= n; i++) {
    r.stock[i].resize(i + 1); r.value[i].resize(i + 1); r.intrinsic[i].resize(i + 1);
    r.continuation[i].assign(i + 1, NAN); r.exercised[i].assign(i + 1, 0);
    for (int j = 0; j <= i; j++) {
      r.stock[i][j] = tree_stock(t, i, j);
      r.intrinsic[i][j] = o.call ? std::max(r.stock[i][j] - o.strike, 0.0) : std::max(o.strike - r.stock[i][j], 0.0);
    }
  }
  r.value[n] = r.intrinsic[n];
  for (int i = n - 1; i >= 0; i--)
    for (int j = 0; j <= i; j++) {
      const double cont = (r.q * r.value[i + 1][j + 1] + (1.0 - r.q) * r.value[i + 1][j]) / (1.0 + t.rate);
      r.continuation[i][j] = cont;
      if (o.american && r.intrinsic[i][j] > cont) { r.value[i][j] = r.intrinsic[i][j]; r.exercised[i][j] = 1; }
      else r.value[i][j] = cont;
    }
  return r;
}
double tree_result_value(const TreeResult &r, const char *what, int step, int ups) {
  QL_REQUIRE(step >= 0 && step <= r.steps, what << ": step " << step << " is outside 0.." << r.steps);
  QL_REQUIRE(ups >= 0 && ups <= step, what << ": ups " << ups << " is outside 0.." << step << " at step " << step);
  const std::string w = what;
  if (w == "value") return r.value[step][ups];
  if (w == "intrinsic") return r.intrinsic[step][ups];
  QL_REQUIRE(step < r.steps, what << ": the last step has no continuation; ask for steps 0.." << r.steps - 1);
  if (w == "continuation") return r.continuation[step][ups];
  return r.exercised[step][ups] ? 1.0 : 0.0;
}
double tree_result_delta(const TreeResult &r, int step, int ups) {
  tree_result_value(r, "delta", step, ups);
  QL_REQUIRE(step < r.steps, "delta: the last step has no children; ask for steps 0.." << r.steps - 1);
  return (r.value[step + 1][ups + 1] - r.value[step + 1][ups]) / (r.stock[step + 1][ups + 1] - r.stock[step + 1][ups]);
}
double tree_result_gamma(const TreeResult &r, int step, int ups) {
  tree_result_value(r, "gamma", step, ups);
  QL_REQUIRE(step < r.steps - 1, "gamma: needs two more steps below the node; ask for steps 0.." << r.steps - 2);
  return (tree_result_delta(r, step + 1, ups + 1) - tree_result_delta(r, step + 1, ups)) / (r.stock[step + 1][ups + 1] - r.stock[step + 1][ups]);
}
double tree_result_bond(const TreeResult &r, int step, int ups) {
  const double d = tree_result_delta(r, step, ups);
  return (r.value[step + 1][ups] - d * r.stock[step + 1][ups]) / (1.0 + r.rate);
}

// 6. Portfolio moments from weights, volatilities and a correlation matrix (n by n, row by row).
namespace {
std::size_t check_portfolio(const std::vector<double> &vols, const std::vector<double> &corr, const char *who) {
  const std::size_t n = vols.size();
  QL_REQUIRE(n >= 1, who << ": need at least one asset");
  QL_REQUIRE(corr.size() == n * n, who << ": the correlation matrix needs " << n * n << " entries (" << n << " by " << n << ", row by row), got " << corr.size());
  for (std::size_t i = 0; i < n; i++) {
    QL_REQUIRE(std::isfinite(vols[i]) && vols[i] >= 0, who << ": volatilities must be finite and not negative");
    QL_REQUIRE(std::fabs(corr[i * n + i] - 1.0) <= 1e-9, who << ": the correlation matrix must have 1 on its diagonal");
    for (std::size_t j = 0; j < n; j++) {
      QL_REQUIRE(std::isfinite(corr[i * n + j]) && std::fabs(corr[i * n + j]) <= 1.0 + 1e-12, who << ": correlations must be in [-1, 1]");
      QL_REQUIRE(std::fabs(corr[i * n + j] - corr[j * n + i]) <= 1e-9, who << ": the correlation matrix must be symmetric");
    }
  }
  return n;
}
void check_weights(const std::vector<double> &w, std::size_t n, const char *who) {
  QL_REQUIRE(w.size() == n, who << ": " << w.size() << " weights for " << n << " assets");
  double s = 0;
  for (double x : w) { QL_REQUIRE(std::isfinite(x), who << ": weights must be finite"); s += x; }
  QL_REQUIRE(std::fabs(s - 1.0) <= 1e-9, who << ": the weights must sum to 1, they sum to " << s);
}
// Solve A x = b (A n by n row major) by Gaussian elimination with partial pivoting; singular matrices are an error.
std::vector<double> solve_linear(std::vector<double> a, std::vector<double> b, std::size_t n, const char *who) {
  double scale = 0;
  for (double x : a) scale = std::max(scale, std::fabs(x));
  for (std::size_t col = 0; col < n; col++) {
    std::size_t piv = col;
    for (std::size_t r = col + 1; r < n; r++) if (std::fabs(a[r * n + col]) > std::fabs(a[piv * n + col])) piv = r;
    QL_REQUIRE(std::fabs(a[piv * n + col]) > 1e-12 * std::max(scale, 1e-300), who << ": the covariance matrix is singular");
    if (piv != col) { for (std::size_t k = 0; k < n; k++) std::swap(a[piv * n + k], a[col * n + k]); std::swap(b[piv], b[col]); }
    for (std::size_t r = col + 1; r < n; r++) {
      const double f = a[r * n + col] / a[col * n + col];
      for (std::size_t k = col; k < n; k++) a[r * n + k] -= f * a[col * n + k];
      b[r] -= f * b[col];
    }
  }
  std::vector<double> x(n);
  for (std::size_t i = n; i-- > 0;) {
    double s = b[i];
    for (std::size_t k = i + 1; k < n; k++) s -= a[i * n + k] * x[k];
    x[i] = s / a[i * n + i];
  }
  return x;
}
std::vector<double> covariance(const std::vector<double> &vols, const std::vector<double> &corr) {
  const std::size_t n = vols.size();
  std::vector<double> s(n * n);
  for (std::size_t i = 0; i < n; i++) for (std::size_t j = 0; j < n; j++) s[i * n + j] = corr[i * n + j] * vols[i] * vols[j];
  return s;
}
std::vector<double> normalise(std::vector<double> z, const char *who) {
  double s = 0;
  for (double x : z) s += x;
  QL_REQUIRE(std::fabs(s) > 1e-14, who << ": the weights cannot be scaled to sum to 1 (their sum is zero)");
  for (double &x : z) x /= s;
  return z;
}
}  // namespace
double portfolio_return(const std::vector<double> &w, const std::vector<double> &r) {
  QL_REQUIRE(!w.empty() && w.size() == r.size(), "portfolio_return: weights and returns need the same, non-zero length (got " << w.size() << " and " << r.size() << ")");
  double s = 0, ws = 0;
  for (std::size_t i = 0; i < w.size(); i++) { s += w[i] * r[i]; ws += w[i]; }
  QL_REQUIRE(std::fabs(ws - 1.0) <= 1e-9, "portfolio_return: the weights must sum to 1, they sum to " << ws);
  return s;
}
double portfolio_variance(const std::vector<double> &w, const std::vector<double> &vols, const std::vector<double> &corr) {
  const std::size_t n = check_portfolio(vols, corr, "portfolio_variance");
  check_weights(w, n, "portfolio_variance");
  double v = 0;
  for (std::size_t i = 0; i < n; i++) for (std::size_t j = 0; j < n; j++) v += w[i] * w[j] * corr[i * n + j] * vols[i] * vols[j];
  return std::max(v, 0.0);   // rounding can leave -1e-19 for a perfectly hedged portfolio
}
// 11. Weights w = S^-1 1 / (1' S^-1 1) (minimum variance) and w = S^-1 (mu - rf 1) / sum (tangency); fully invested,
// short sales allowed. Two assets use the closed form (the adjugate), which stays valid when rho = -1 or 1.
std::vector<double> min_variance_weights(const std::vector<double> &vols, const std::vector<double> &corr) {
  const std::size_t n = check_portfolio(vols, corr, "min_variance_weights");
  if (n == 1) return {1.0};
  if (n == 2) {
    const double c = corr[1] * vols[0] * vols[1];
    return normalise({vols[1] * vols[1] - c, vols[0] * vols[0] - c}, "min_variance_weights");
  }
  return normalise(solve_linear(covariance(vols, corr), std::vector<double>(n, 1.0), n, "min_variance_weights"), "min_variance_weights");
}
std::vector<double> tangency_weights(const std::vector<double> &mu, const std::vector<double> &vols, const std::vector<double> &corr, double rf) {
  const std::size_t n = check_portfolio(vols, corr, "tangency_weights");
  QL_REQUIRE(mu.size() == n, "tangency_weights: " << mu.size() << " returns for " << n << " assets");
  QL_REQUIRE(std::isfinite(rf), "tangency_weights: risk_free must be finite");
  std::vector<double> ex(n);
  for (std::size_t i = 0; i < n; i++) { QL_REQUIRE(std::isfinite(mu[i]), "tangency_weights: returns must be finite"); ex[i] = mu[i] - rf; }
  if (n == 1) return {1.0};
  if (n == 2) {
    const double c = corr[1] * vols[0] * vols[1];
    return normalise({ex[0] * vols[1] * vols[1] - ex[1] * c, ex[1] * vols[0] * vols[0] - ex[0] * c}, "tangency_weights");
  }
  return normalise(solve_linear(covariance(vols, corr), ex, n, "tangency_weights"), "tangency_weights");
}

// 9. Discrete loss distribution (positive is a loss). VaR is the smallest loss whose cumulative probability reaches the
// confidence (1e-12 tolerance at the boundary); ES is the mean loss over the worst 1 - confidence of probability, the
// boundary scenario split.
LossDist loss_distribution(const std::vector<double> &losses, const std::vector<double> &probs) {
  QL_REQUIRE(!losses.empty() && losses.size() == probs.size(), "loss_distribution: losses and probabilities need the same, non-zero length (got " << losses.size() << " and " << probs.size() << ")");
  double s = 0;
  LossDist d;
  for (std::size_t i = 0; i < losses.size(); i++) {
    QL_REQUIRE(std::isfinite(losses[i]) && std::isfinite(probs[i]), "loss_distribution: values must be finite");
    QL_REQUIRE(probs[i] >= 0, "loss_distribution: probabilities must not be negative");
    s += probs[i];
    d.v.emplace_back(losses[i], probs[i]);
  }
  QL_REQUIRE(std::fabs(s - 1.0) <= 1e-9, "loss_distribution: the probabilities must sum to 1, they sum to " << s);
  std::stable_sort(d.v.begin(), d.v.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  return d;
}
double loss_mean(const LossDist &d) { double m = 0; for (const auto &x : d.v) m += x.first * x.second; return m; }
double loss_cdf(const LossDist &d, double x) { double s = 0; for (const auto &p : d.v) if (p.first <= x) s += p.second; return s; }
double loss_var(const LossDist &d, double conf) {
  QL_REQUIRE(conf > 0 && conf < 1, "var: confidence must be inside (0, 1)");
  double cum = 0;
  for (const auto &p : d.v) { cum += p.second; if (cum >= conf - 1e-12) return p.first; }
  return d.v.back().first;
}
double loss_es(const LossDist &d, double conf) {
  QL_REQUIRE(conf > 0 && conf < 1, "es: confidence must be inside (0, 1)");
  double rest = 1.0 - conf, acc = 0;
  for (std::size_t i = d.v.size(); i-- > 0 && rest > 1e-14;) {
    const double take = std::min(d.v[i].second, rest);
    acc += take * d.v[i].first;
    rest -= take;
  }
  return acc / (1.0 - conf);
}

// 18. Bond cash flows after the evaluation date, same-date flows merged (the last coupon and the redemption are one amount);
// times are 30/360 years from the evaluation date, so a bond issued today with annual coupons has times 1, 2, 3.
namespace {
std::vector<std::pair<Date, double>> bond_flows(const Clock &c, const lmfinance::Bond &b) {
  activate(c);
  std::vector<std::pair<Date, double>> out;
  const auto bond = make_bond(c, b);   // keep the bond alive: cashflows() returns a reference into it
  for (const auto &cf : bond->cashflows()) {
    if (cf->date() <= c.today) continue;
    if (!out.empty() && out.back().first == cf->date()) out.back().second += cf->amount();
    else out.emplace_back(cf->date(), cf->amount());
  }
  return out;
}
}  // namespace
std::vector<double> bond_cashflow_times(const Clock &c, const lmfinance::Bond &b) {
  std::vector<double> t;
  for (const auto &f : bond_flows(c, b)) t.push_back(bond_dc().yearFraction(c.today, f.first));
  return t;
}
std::vector<double> bond_cashflow_amounts(const Clock &c, const lmfinance::Bond &b) {
  std::vector<double> a;
  for (const auto &f : bond_flows(c, b)) a.push_back(f.second);
  return a;
}
// 19. Par yield: the coupon rate (freq payments a year) at which a bond on the curve prices at par: freq (1 - D_n) / sum D_t.
double par_yield(const Curve &cv, double years, int freq) {
  QL_REQUIRE(freq == 1 || freq == 2 || freq == 4 || freq == 12, "par_yield: payments per year must be 1, 2, 4 or 12");
  const double n = years * freq;
  const int p = static_cast<int>(std::lround(n));
  QL_REQUIRE(p >= 1 && std::fabs(n - p) < 1e-9 && years <= 100, "par_yield: years must be a whole number of payment periods");
  double sum = 0;
  for (int k = 1; k <= p; k++) sum += curve_discount(cv, static_cast<double>(k) / freq);
  return freq * (1.0 - curve_discount(cv, static_cast<double>(p) / freq)) / sum;
}

}  // namespace lmfinance
