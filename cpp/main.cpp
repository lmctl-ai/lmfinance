// lmfinance: QuantLib behind lmscript `_dsl_` schemas (market, instrument, model, pricing, risk, calibration).
// Scripting only: the executable has no user-facing commands besides --dsl and the schema/version queries.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "dslvm.hpp"
#include "finance.hpp"
#include "memory_budget.hpp"
#include "notices_embedded.hpp"
#include "schema_embedded.hpp"

namespace {
using dslvm::Args;
using dslvm::Value;
using namespace lmfinance;

struct Host {
  Clock clock{QuantLib::Date(2, QuantLib::January, 2024)};
  std::vector<std::unique_ptr<Curve>> curves;
  std::vector<std::unique_ptr<CurveBuilder>> builders;
  std::vector<std::unique_ptr<Option>> options;
  std::vector<std::unique_ptr<Bond>> bonds;
  std::vector<std::unique_ptr<Swap>> swaps;
  std::vector<std::unique_ptr<Model>> models;
  std::vector<std::unique_ptr<Engine>> engines;
  std::vector<std::unique_ptr<Result>> results;
  std::vector<std::unique_ptr<HestonFit>> fits;
  template <class T> Value hold(std::vector<std::unique_ptr<T>> &arena, const char *type, T v) {
    if (arena.size() >= 4000000) throw dslvm::Error(std::string(type) + " object limit exceeded");
    arena.push_back(std::make_unique<T>(std::move(v)));
    return Value::object(type, arena.back().get());
  }
};

template <class F> Value translate(F &&fn) {
  try { return fn(); }
  catch (const dslvm::Error &) { throw; }
  catch (const std::exception &e) { throw dslvm::Error(e.what()); }
}

void check_args(const char *who, const std::vector<std::string> &params, Args &a) {
  for (size_t i = 0; i < params.size(); i++) {
    const auto &want = params[i];
    const auto &v = a[i];
    bool ok = want == "float" ? v.kind == Value::FLOAT
              : want == "int" ? v.kind == Value::INT
              : want == "string" ? v.kind == Value::STR
              : want == "bool" ? v.kind == Value::BOOL
              : v.kind == Value::OBJ && v.type == want && v.obj;
    if (!ok) throw dslvm::Error(std::string(who) + " argument " + std::to_string(i + 1) + " must be " + want);
    // No lmfinance input is meaningful as NaN or infinity; QuantLib would propagate it into silent NaN prices.
    if (want == "float" && !std::isfinite(v.f))
      throw dslvm::Error(std::string(who) + " argument " + std::to_string(i + 1) + " must be finite");
  }
}

void method(dslvm::Registry &r, const char *type, const char *name, std::vector<std::string> params, std::string returns,
            dslvm::Method cb) {
  std::string who = std::string(type) + "." + name;
  r.method(type, name, params, returns, [who, params, cb](void *self, Args &a) {
    check_args(who.c_str(), params, a);
    return translate([&] { return cb(self, a); });
  });
}

template <class T> T &obj(Value &v) { return *static_cast<T *>(v.obj); }
template <class T> T &self(void *p) { return *static_cast<T *>(p); }
std::string fmt(double x) { char b[40]; std::snprintf(b, sizeof b, "%.10g", x); return b; }
void need(bool ok, const std::string &msg) { if (!ok) throw dslvm::Error(msg); }

double provided(double v, const char *name) {
  if (std::isnan(v)) throw dslvm::Error(std::string("Result.") + name + ": this engine does not provide it; use the risk functions (bumped Greeks)");
  return v;
}
Engine make_engine(EngineKind k) { Engine e; e.kind = k; return e; }

void register_host(dslvm::Registry &r, Host &h) {
  r.context("market", "MarketApi");
  r.context("instrument", "InstrumentApi");
  r.context("model", "ModelApi");
  r.context("pricing", "PricingApi");
  r.context("risk", "RiskApi");
  r.context("calibration", "CalibrationApi");
  r.context("lmf", "LmfApi");

  // ---------------------------------------------------------------- market
  method(r, "MarketApi", "set_date", {"string"}, "void", [&h](void *, Args &a) {
    h.clock.today = parse_date(a[0].s); return Value::none();
  });
  method(r, "MarketApi", "date", {}, "string", [&h](void *, Args &) { return Value::string(format_date(h.clock.today)); });
  method(r, "MarketApi", "flat_curve", {"float"}, "Curve", [&h](void *, Args &a) { return h.hold(h.curves, "Curve", flat_curve(h.clock, a[0].f)); });
  method(r, "MarketApi", "zero_curve", {}, "CurveBuilder", [&h](void *, Args &) { return h.hold(h.builders, "CurveBuilder", CurveBuilder{}); });
  method(r, "MarketApi", "bootstrap_curve", {}, "CurveBuilder", [&h](void *, Args &) {
    CurveBuilder b; b.bootstrap = true; return h.hold(h.builders, "CurveBuilder", std::move(b));
  });

  method(r, "CurveBuilder", "zero", {"float", "float"}, "CurveBuilder", [](void *p, Args &a) {
    need(!self<CurveBuilder>(p).bootstrap, "CurveBuilder.zero: this builder bootstraps from deposit() and swap() quotes; use market.zero_curve()");
    self<CurveBuilder>(p).zeros.emplace_back(a[0].f, a[1].f); return Value::object("CurveBuilder", p);
  });
  method(r, "CurveBuilder", "deposit", {"string", "float"}, "CurveBuilder", [](void *p, Args &a) {
    need(self<CurveBuilder>(p).bootstrap, "CurveBuilder.deposit: use market.bootstrap_curve()");
    self<CurveBuilder>(p).deposits.emplace_back(a[0].s, a[1].f); return Value::object("CurveBuilder", p);
  });
  method(r, "CurveBuilder", "swap", {"string", "float"}, "CurveBuilder", [](void *p, Args &a) {
    need(self<CurveBuilder>(p).bootstrap, "CurveBuilder.swap: use market.bootstrap_curve()");
    self<CurveBuilder>(p).swaps.emplace_back(a[0].s, a[1].f); return Value::object("CurveBuilder", p);
  });
  method(r, "CurveBuilder", "build", {}, "Curve", [&h](void *p, Args &) { return h.hold(h.curves, "Curve", build_curve(h.clock, self<CurveBuilder>(p))); });

  method(r, "Curve", "discount", {"float"}, "float", [](void *p, Args &a) { return Value::real(curve_discount(self<Curve>(p), a[0].f)); });
  method(r, "Curve", "zero", {"float"}, "float", [](void *p, Args &a) { return Value::real(curve_zero(self<Curve>(p), a[0].f)); });
  method(r, "Curve", "forward", {"float", "float"}, "float", [](void *p, Args &a) { return Value::real(curve_forward(self<Curve>(p), a[0].f, a[1].f)); });
  method(r, "Curve", "bumped", {"float"}, "Curve", [&h](void *p, Args &a) { return h.hold(h.curves, "Curve", bump_curve(self<Curve>(p), a[0].f)); });
  method(r, "Curve", "save_csv", {"string", "float", "int"}, "void", [](void *p, Args &a) {
    curve_save_csv(self<Curve>(p), a[0].s, a[1].f, (int)a[2].i); return Value::none();
  });

  // ---------------------------------------------------------------- instrument
  auto vanilla = [&h](bool call, bool american, Args &a) {
    need(a[0].f > 0, "option strike must be positive");
    need(a[1].f > 0, "option maturity must be positive");
    Option o; o.call = call; o.american = american; o.strike = a[0].f; o.years = a[1].f;
    return h.hold(h.options, "Option", o);
  };
  method(r, "InstrumentApi", "european_call", {"float", "float"}, "Option", [vanilla](void *, Args &a) { return vanilla(true, false, a); });
  method(r, "InstrumentApi", "european_put", {"float", "float"}, "Option", [vanilla](void *, Args &a) { return vanilla(false, false, a); });
  method(r, "InstrumentApi", "american_call", {"float", "float"}, "Option", [vanilla](void *, Args &a) { return vanilla(true, true, a); });
  method(r, "InstrumentApi", "american_put", {"float", "float"}, "Option", [vanilla](void *, Args &a) { return vanilla(false, true, a); });
  method(r, "InstrumentApi", "bond", {"float", "float", "float", "int"}, "Bond", [&h](void *, Args &a) {
    Bond b; b.face = a[0].f; b.coupon = a[1].f; b.years = a[2].f; b.freq = (int)a[3].i;
    return h.hold(h.bonds, "Bond", b);
  });
  method(r, "InstrumentApi", "zero_bond", {"float", "float"}, "Bond", [&h](void *, Args &a) {
    Bond b; b.face = a[0].f; b.years = a[1].f; b.zero = true;
    return h.hold(h.bonds, "Bond", b);
  });
  method(r, "InstrumentApi", "swap", {"float", "float", "float"}, "Swap", [&h](void *, Args &a) {
    Swap s; s.notional = a[0].f; s.rate = a[1].f; s.years = a[2].f;
    return h.hold(h.swaps, "Swap", s);
  });
  method(r, "Option", "strike", {}, "float", [](void *p, Args &) { return Value::real(self<Option>(p).strike); });
  method(r, "Option", "years", {}, "float", [](void *p, Args &) { return Value::real(self<Option>(p).years); });
  method(r, "Bond", "years", {}, "float", [](void *p, Args &) { return Value::real(self<Bond>(p).years); });
  method(r, "Swap", "receiver", {}, "Swap", [](void *p, Args &) { self<Swap>(p).receiver = true; return Value::object("Swap", p); });
  method(r, "Swap", "payer", {}, "Swap", [](void *p, Args &) { self<Swap>(p).receiver = false; return Value::object("Swap", p); });
  method(r, "Swap", "spread", {"float"}, "Swap", [](void *p, Args &a) { self<Swap>(p).spread = a[0].f; return Value::object("Swap", p); });

  // ---------------------------------------------------------------- model
  method(r, "ModelApi", "black_scholes", {"float", "float", "float", "float"}, "Model", [&h](void *, Args &a) {
    Model m; m.spot = a[0].f; m.r = a[1].f; m.q = a[2].f; m.vol = a[3].f;
    need(m.spot > 0, "model.black_scholes: spot must be positive");
    need(m.vol > 0, "model.black_scholes: volatility must be positive");
    return h.hold(h.models, "Model", m);
  });
  method(r, "ModelApi", "heston", {"float", "float", "float", "float", "float", "float", "float", "float"}, "Model", [&h](void *, Args &a) {
    Model m; m.heston = true; m.spot = a[0].f; m.r = a[1].f; m.q = a[2].f;
    m.v0 = a[3].f; m.kappa = a[4].f; m.theta = a[5].f; m.sigma = a[6].f; m.rho = a[7].f;
    need(m.spot > 0, "model.heston: spot must be positive");
    need(m.v0 > 0 && m.kappa > 0 && m.theta > 0 && m.sigma > 0, "model.heston: v0, kappa, theta and sigma must be positive");
    need(m.rho > -1 && m.rho < 1, "model.heston: rho must be inside (-1, 1)");
    return h.hold(h.models, "Model", m);
  });
  method(r, "Model", "spot", {}, "float", [](void *p, Args &) { return Value::real(self<Model>(p).spot); });
  method(r, "Model", "param", {"string"}, "float", [](void *p, Args &a) {
    const Model &m = self<Model>(p); const std::string &n = a[0].s;
    if (n == "spot") return Value::real(m.spot);
    if (n == "rate") return Value::real(m.r);
    if (n == "dividend") return Value::real(m.q);
    if (!m.heston && n == "vol") return Value::real(m.vol);
    if (m.heston && n == "v0") return Value::real(m.v0);
    if (m.heston && n == "kappa") return Value::real(m.kappa);
    if (m.heston && n == "theta") return Value::real(m.theta);
    if (m.heston && n == "sigma") return Value::real(m.sigma);
    if (m.heston && n == "rho") return Value::real(m.rho);
    throw dslvm::Error("Model.param: unknown parameter '" + n + "' for a " + (m.heston ? "Heston" : "Black-Scholes") + " model");
  });
  method(r, "Model", "with_spot", {"float"}, "Model", [&h](void *p, Args &a) { Model m = self<Model>(p); m.spot = a[0].f; return h.hold(h.models, "Model", m); });
  method(r, "Model", "with_rate", {"float"}, "Model", [&h](void *p, Args &a) { Model m = self<Model>(p); m.r = a[0].f; return h.hold(h.models, "Model", m); });
  method(r, "Model", "with_vol", {"float"}, "Model", [&h](void *p, Args &a) {
    Model m = self<Model>(p); need(!m.heston, "Model.with_vol: a Heston model has no single volatility");
    m.vol = a[0].f; return h.hold(h.models, "Model", m);
  });

  // ---------------------------------------------------------------- pricing
  method(r, "PricingApi", "analytic", {}, "Engine", [&h](void *, Args &) { return h.hold(h.engines, "Engine", make_engine(EngineKind::Analytic)); });
  method(r, "PricingApi", "baw", {}, "Engine", [&h](void *, Args &) { return h.hold(h.engines, "Engine", make_engine(EngineKind::BaroneAdesiWhaley)); });
  method(r, "PricingApi", "binomial", {"int"}, "Engine", [&h](void *, Args &a) {
    need(a[0].i >= 2 && a[0].i <= 20000, "pricing.binomial: steps must be 2..20000");
    Engine e = make_engine(EngineKind::Binomial); e.steps = (int)a[0].i; return h.hold(h.engines, "Engine", e);
  });
  method(r, "PricingApi", "binomial_tree", {"string", "int"}, "Engine", [&h](void *, Args &a) {
    need(a[1].i >= 2 && a[1].i <= 20000, "pricing.binomial_tree: steps must be 2..20000");
    need(a[0].s == "crr" || a[0].s == "jr" || a[0].s == "tian" || a[0].s == "lr", "pricing.binomial_tree: tree must be crr, jr, tian or lr");
    Engine e = make_engine(EngineKind::Binomial); e.tree = a[0].s; e.steps = (int)a[1].i; return h.hold(h.engines, "Engine", e);
  });
  method(r, "PricingApi", "finite_diff", {"int", "int"}, "Engine", [&h](void *, Args &a) {
    need(a[0].i >= 2 && a[0].i <= 100000 && a[1].i >= 10 && a[1].i <= 100000, "pricing.finite_diff: need 2..100000 time steps and 10..100000 grid points");
    Engine e = make_engine(EngineKind::FiniteDifference); e.steps = (int)a[0].i; e.grid = (int)a[1].i; return h.hold(h.engines, "Engine", e);
  });
  method(r, "PricingApi", "monte_carlo", {"int", "int"}, "Engine", [&h](void *, Args &a) {
    need(a[0].i >= 16 && a[0].i <= 100000000, "pricing.monte_carlo: paths must be 16..100000000");
    need(a[1].i >= 1, "pricing.monte_carlo: seed must be at least 1 (0 would mean a clock-based seed)");
    Engine e = make_engine(EngineKind::MonteCarlo); e.paths = a[0].i; e.seed = a[1].i; return h.hold(h.engines, "Engine", e);
  });
  method(r, "PricingApi", "price", {"Option", "Model", "Engine"}, "Result", [&h](void *, Args &a) {
    return h.hold(h.results, "Result", price_option(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2])));
  });
  method(r, "PricingApi", "bond_clean_price", {"Bond", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(bond_clean_price(h.clock, obj<Bond>(a[0]), obj<Curve>(a[1]))); });
  method(r, "PricingApi", "bond_npv", {"Bond", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(bond_npv(h.clock, obj<Bond>(a[0]), obj<Curve>(a[1]))); });
  method(r, "PricingApi", "bond_accrued", {"Bond"}, "float", [&h](void *, Args &a) { return Value::real(bond_accrued(h.clock, obj<Bond>(a[0]))); });
  method(r, "PricingApi", "bond_price_from_yield", {"Bond", "float"}, "float", [&h](void *, Args &a) { return Value::real(bond_clean_from_yield(h.clock, obj<Bond>(a[0]), a[1].f)); });
  method(r, "PricingApi", "bond_yield", {"Bond", "float"}, "float", [&h](void *, Args &a) { return Value::real(bond_yield(h.clock, obj<Bond>(a[0]), a[1].f)); });
  method(r, "PricingApi", "bond_duration", {"Bond", "float"}, "float", [&h](void *, Args &a) { return Value::real(bond_duration(h.clock, obj<Bond>(a[0]), a[1].f, false)); });
  method(r, "PricingApi", "bond_macaulay_duration", {"Bond", "float"}, "float", [&h](void *, Args &a) { return Value::real(bond_duration(h.clock, obj<Bond>(a[0]), a[1].f, true)); });
  method(r, "PricingApi", "bond_convexity", {"Bond", "float"}, "float", [&h](void *, Args &a) { return Value::real(bond_convexity(h.clock, obj<Bond>(a[0]), a[1].f)); });
  method(r, "PricingApi", "swap_npv", {"Swap", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(swap_npv(h.clock, obj<Swap>(a[0]), obj<Curve>(a[1]))); });
  method(r, "PricingApi", "swap_par_rate", {"Swap", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(swap_par_rate(h.clock, obj<Swap>(a[0]), obj<Curve>(a[1]))); });
  method(r, "PricingApi", "swap_annuity", {"Swap", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(swap_annuity(h.clock, obj<Swap>(a[0]), obj<Curve>(a[1]))); });

  method(r, "Engine", "name", {}, "string", [](void *p, Args &) {
    static const char *n[] = {"analytic", "binomial", "finite_diff", "monte_carlo", "baw"};
    return Value::string(n[static_cast<int>(self<Engine>(p).kind)]);
  });
  method(r, "Result", "price", {}, "float", [](void *p, Args &) { return Value::real(self<Result>(p).price); });
  method(r, "Result", "stderr", {}, "float", [](void *p, Args &) { return Value::real(provided(self<Result>(p).stderr_, "stderr")); });
  method(r, "Result", "delta", {}, "float", [](void *p, Args &) { return Value::real(provided(self<Result>(p).delta, "delta")); });
  method(r, "Result", "gamma", {}, "float", [](void *p, Args &) { return Value::real(provided(self<Result>(p).gamma, "gamma")); });
  method(r, "Result", "vega", {}, "float", [](void *p, Args &) { return Value::real(provided(self<Result>(p).vega, "vega")); });
  method(r, "Result", "theta", {}, "float", [](void *p, Args &) { return Value::real(provided(self<Result>(p).theta, "theta")); });
  method(r, "Result", "rho", {}, "float", [](void *p, Args &) { return Value::real(provided(self<Result>(p).rho, "rho")); });

  // ---------------------------------------------------------------- risk
  method(r, "RiskApi", "delta", {"Option", "Model", "Engine"}, "float", [&h](void *, Args &a) { return Value::real(bumped_delta(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2]))); });
  method(r, "RiskApi", "gamma", {"Option", "Model", "Engine"}, "float", [&h](void *, Args &a) { return Value::real(bumped_gamma(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2]))); });
  method(r, "RiskApi", "vega", {"Option", "Model", "Engine"}, "float", [&h](void *, Args &a) { return Value::real(bumped_vega(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2]))); });
  method(r, "RiskApi", "rho", {"Option", "Model", "Engine"}, "float", [&h](void *, Args &a) { return Value::real(bumped_rho(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2]))); });
  method(r, "RiskApi", "theta", {"Option", "Model", "Engine"}, "float", [&h](void *, Args &a) { return Value::real(bumped_theta(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2]))); });
  method(r, "RiskApi", "save_profile", {"Option", "Model", "Engine", "float", "float", "int", "string"}, "void", [&h](void *, Args &a) {
    save_profile(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2]), a[3].f, a[4].f, (int)a[5].i, a[6].s); return Value::none();
  });
  method(r, "RiskApi", "bond_dv01", {"Bond", "Curve"}, "float", [&h](void *, Args &a) {
    Curve up = bump_curve(obj<Curve>(a[1]), 1e-4), dn = bump_curve(obj<Curve>(a[1]), -1e-4);
    return Value::real((bond_npv(h.clock, obj<Bond>(a[0]), dn) - bond_npv(h.clock, obj<Bond>(a[0]), up)) / 2.0);
  });
  method(r, "RiskApi", "swap_dv01", {"Swap", "Curve"}, "float", [&h](void *, Args &a) {
    Curve up = bump_curve(obj<Curve>(a[1]), 1e-4), dn = bump_curve(obj<Curve>(a[1]), -1e-4);
    return Value::real((swap_npv(h.clock, obj<Swap>(a[0]), up) - swap_npv(h.clock, obj<Swap>(a[0]), dn)) / 2.0);
  });
  method(r, "RiskApi", "var_normal", {"float", "float", "float", "float"}, "float", [](void *, Args &a) {
    need(a[1].f >= 0, "risk.var_normal: volatility must not be negative");
    need(a[2].f > 0.5 && a[2].f < 1, "risk.var_normal: confidence must be inside (0.5, 1)");
    need(a[3].f > 0, "risk.var_normal: horizon must be positive");
    return Value::real(a[0].f * a[1].f * std::sqrt(a[3].f / 252.0) * normal_quantile(a[2].f));
  });
  method(r, "RiskApi", "es_normal", {"float", "float", "float", "float"}, "float", [](void *, Args &a) {
    need(a[1].f >= 0, "risk.es_normal: volatility must not be negative");
    need(a[2].f > 0.5 && a[2].f < 1, "risk.es_normal: confidence must be inside (0.5, 1)");
    need(a[3].f > 0, "risk.es_normal: horizon must be positive");
    return Value::real(a[0].f * a[1].f * std::sqrt(a[3].f / 252.0) * normal_density(normal_quantile(a[2].f)) / (1.0 - a[2].f));
  });

  // ---------------------------------------------------------------- calibration
  method(r, "CalibrationApi", "implied_vol", {"Option", "Model", "Engine", "float"}, "float", [&h](void *, Args &a) {
    return Value::real(implied_vol(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2]), a[3].f));
  });
  method(r, "CalibrationApi", "heston", {"float", "float", "float"}, "HestonFit", [&h](void *, Args &a) {
    need(a[0].f > 0, "calibration.heston: spot must be positive");
    HestonFit f; f.spot = a[0].f; f.r = a[1].f; f.q = a[2].f;
    return h.hold(h.fits, "HestonFit", f);
  });
  method(r, "HestonFit", "quote", {"float", "float", "float"}, "HestonFit", [](void *p, Args &a) {
    need(a[0].f > 0 && a[1].f > 0 && a[2].f > 0, "HestonFit.quote: strike, years and volatility must be positive");
    self<HestonFit>(p).quotes.push_back({a[0].f, a[1].f, a[2].f}); return Value::object("HestonFit", p);
  });
  method(r, "HestonFit", "guess", {"float", "float", "float", "float", "float"}, "HestonFit", [](void *p, Args &a) {
    HestonFit &f = self<HestonFit>(p);
    need(a[0].f > 0 && a[1].f > 0 && a[2].f > 0 && a[3].f > 0, "HestonFit.guess: v0, kappa, theta and sigma must be positive");
    need(a[4].f > -1 && a[4].f < 1, "HestonFit.guess: rho must be inside (-1, 1)");
    f.v0 = a[0].f; f.kappa = a[1].f; f.theta = a[2].f; f.sigma = a[3].f; f.rho = a[4].f; return Value::object("HestonFit", p);
  });
  method(r, "HestonFit", "fit", {}, "Model", [&h](void *p, Args &) {
    HestonFit &f = self<HestonFit>(p);
    heston_fit(h.clock, f);
    Model m; m.heston = true; m.spot = f.spot; m.r = f.r; m.q = f.q; m.v0 = f.v0; m.kappa = f.kappa; m.theta = f.theta; m.sigma = f.sigma; m.rho = f.rho;
    return h.hold(h.models, "Model", m);
  });
  method(r, "HestonFit", "error", {}, "float", [](void *p, Args &) {
    need(!std::isnan(self<HestonFit>(p).error), "HestonFit.error: call fit() first");
    return Value::real(self<HestonFit>(p).error);
  });

  // ---------------------------------------------------------------- lmf (checks and output)
  method(r, "LmfApi", "assert_close", {"float", "float", "float"}, "void", [](void *, Args &a) {
    if (!(std::fabs(a[0].f - a[1].f) <= a[2].f))
      throw dslvm::Error("assert_close failed: " + fmt(a[0].f) + " vs " + fmt(a[1].f) + " (tolerance " + fmt(a[2].f) + ")");
    return Value::none();
  });
  method(r, "LmfApi", "check", {"bool", "string"}, "void", [](void *, Args &a) {
    if (!a[0].b) throw dslvm::Error("check failed: " + a[1].s);
    return Value::none();
  });
  method(r, "LmfApi", "save_text", {"string", "string"}, "void", [](void *, Args &a) {
    std::ofstream f(a[0].s, std::ios::binary);
    if (!f) throw dslvm::Error("lmf.save_text: cannot write " + a[0].s);
    f << a[1].s;
    f.flush();
    if (!f) throw dslvm::Error("lmf.save_text: write failed for " + a[0].s);
    return Value::none();
  });
  method(r, "LmfApi", "print", {"string"}, "void", [](void *, Args &a) { std::cout << a[0].s << std::endl; return Value::none(); });
  method(r, "LmfApi", "version", {}, "string", [](void *, Args &) { return Value::string("lmfinance " LMFINANCE_VERSION); });
}

const char *usage = "usage: lmfinance --version | --notices | --dsl-schema [--json] | --dsl-signatures | [--dry-run] --dsl FILE [FILE ...]\n"
                    "environment: LMFINANCE_MAX_MEMORY_MB  total memory budget in MiB (default 4096); a program that needs\n"
                    "             more stops with a run error (exit 3) instead of allocating\n";
}  // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  Host host;
  dslvm::Registry reg;
  register_host(reg, host);
  std::string a1 = argc > 1 ? argv[1] : "";
  if (argc == 2 && a1 == "--version") { std::cout << "lmfinance " LMFINANCE_VERSION "\n"; return std::cout ? 0 : 4; }
  if (argc == 2 && a1 == "--notices") {
    std::cout.write(reinterpret_cast<const char *>(lmfinance::embedded_notices), (std::streamsize)lmfinance::embedded_notices_size);
    return std::cout ? 0 : 4;
  }
  if (argc >= 2 && a1 == "--dsl-schema") {
    if (argc == 2) { for (const auto &s : reg.signatures()) std::cout << s << "\n"; return std::cout ? 0 : 4; }
    if (argc == 3 && std::string(argv[2]) == "--json") {
      std::cout.write(reinterpret_cast<const char *>(lmfinance::embedded_schema), (std::streamsize)lmfinance::embedded_schema_size);
      return std::cout ? 0 : 4;
    }
    std::cerr << usage; return 2;
  }
  if (argc == 2 && a1 == "--dsl-signatures") { for (const auto &s : reg.signatures()) std::cout << s << "\n"; return 0; }
  int first = 2;
  const bool dry = argc >= 2 && a1 == "--dry-run";  // load and verify the bytecode, run nothing
  if (dry) first = 3;
  if (argc <= first || std::string(argv[first - 1]) != "--dsl") { std::cerr << usage; return 2; }
  for (int i = first; i < argc; i++)
    if (std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0) { std::cerr << usage; return 2; }
  if (!membudget::configure("lmfinance", "LMFINANCE_MAX_MEMORY_MB")) { std::cerr << usage; return 2; }
  const std::string schema(reinterpret_cast<const char *>(lmfinance::embedded_schema), lmfinance::embedded_schema_size);
  dslvm::Context context;
  for (const char *n : {"market", "instrument", "model", "pricing", "risk", "calibration", "lmf"}) context.set(n, &host);
  std::vector<dslvm::Program> programs;
  for (int i = first; i < argc; i++) {
    try { programs.push_back(dslvm::Program::load(dslvm::read_file(argv[i]), schema, reg)); }
    catch (const dslvm::Error &e) { return dslvm::report_error("load", e); }
    catch (const std::exception &e) { return dslvm::report_error("load", dslvm::Error(e.what())); }
  }
  if (dry) { dslvm::report_ok(); return 0; }
  for (const auto &p : programs) {
    try { p.run(context, reg); }
    catch (const dslvm::Error &e) { return dslvm::report_error("run", e); }
    catch (const std::exception &e) { return dslvm::report_error("run", dslvm::Error(e.what())); }  // budget in the VM
    dslvm::report_ok();
  }
  return 0;
}
