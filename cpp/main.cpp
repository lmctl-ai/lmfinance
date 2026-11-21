// lmfinance: QuantLib behind lmscript `_lmcode_` schemas (market, instrument, model, pricing, risk, calibration).
// Scripting only: the executable has no user-facing commands besides --lmcode and the schema/version queries.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
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
  std::vector<std::unique_ptr<CapFloor>> capfloors;
  std::vector<std::unique_ptr<Swaption>> swaptions;
  std::vector<std::unique_ptr<Series>> series;
  std::vector<std::unique_ptr<Strategy>> strategies;
  std::vector<std::unique_ptr<Tree>> trees;
  std::vector<std::unique_ptr<LossDist>> loss_dists;
  std::vector<std::unique_ptr<TreeResult>> tree_results;
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
// An int argument that must fit a C++ int: a wrapped 64-bit value would otherwise pass a range check by accident.
int as_int(const Value &v) {
  if (v.i < -2147483647LL || v.i > 2147483647LL) throw dslvm::Error("integer argument " + std::to_string(v.i) + " is out of range");
  return static_cast<int>(v.i);
}
using FloatList = dslvm::stdlib::FloatList;
const std::vector<double> &flist(Value &v) { return static_cast<FloatList *>(v.obj)->items; }
Value make_flist(dslvm::Registry &r, std::vector<double> v) { return Value::object("FloatList", r.std_collections().make_float_list(std::move(v))); }
const std::vector<double> &finite_list(Value &v, const char *who) {
  const auto &x = flist(v);
  for (double d : x) if (!std::isfinite(d)) throw dslvm::Error(std::string(who) + ": list values must be finite");
  return x;
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
  method(r, "MarketApi", "flat_curve_compounded", {"float", "string"}, "Curve", [&h](void *, Args &a) {
    return h.hold(h.curves, "Curve", flat_curve_compounded(h.clock, a[0].f, a[1].s));
  });
  method(r, "MarketApi", "equivalent_rate", {"float", "string", "string"}, "float", [](void *, Args &a) {
    return Value::real(equivalent_rate(a[0].f, a[1].s, a[2].s));
  });
  method(r, "MarketApi", "discount_curve", {}, "CurveBuilder", [&h](void *, Args &) {
    CurveBuilder b; b.discount = true; return h.hold(h.builders, "CurveBuilder", std::move(b));
  });
  method(r, "MarketApi", "zero_curve", {}, "CurveBuilder", [&h](void *, Args &) { return h.hold(h.builders, "CurveBuilder", CurveBuilder{}); });
  method(r, "MarketApi", "bootstrap_curve", {}, "CurveBuilder", [&h](void *, Args &) {
    CurveBuilder b; b.bootstrap = true; return h.hold(h.builders, "CurveBuilder", std::move(b));
  });

  method(r, "CurveBuilder", "zero", {"float", "float"}, "CurveBuilder", [](void *p, Args &a) {
    need(!self<CurveBuilder>(p).bootstrap && !self<CurveBuilder>(p).discount, "CurveBuilder.zero: use market.zero_curve()");
    self<CurveBuilder>(p).zeros.emplace_back(a[0].f, a[1].f); return Value::object("CurveBuilder", p);
  });
  method(r, "CurveBuilder", "discount", {"float", "float"}, "CurveBuilder", [](void *p, Args &a) {
    need(self<CurveBuilder>(p).discount, "CurveBuilder.discount: use market.discount_curve()");
    need(a[0].f > 0, "CurveBuilder.discount: years must be positive");
    self<CurveBuilder>(p).discounts.emplace_back(a[0].f, a[1].f); return Value::object("CurveBuilder", p);
  });
  method(r, "CurveBuilder", "bond", {"float", "float", "float"}, "CurveBuilder", [](void *p, Args &a) {
    need(self<CurveBuilder>(p).bootstrap, "CurveBuilder.bond: use market.bootstrap_curve()");
    need(a[0].f > 0 && a[2].f > 0, "CurveBuilder.bond: years and clean price must be positive");
    self<CurveBuilder>(p).bonds.push_back({a[0].f, a[1].f, a[2].f}); return Value::object("CurveBuilder", p);
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
  method(r, "Curve", "zero_rate", {"float", "string"}, "float", [](void *p, Args &a) { return Value::real(curve_zero_rate(self<Curve>(p), a[0].f, a[1].s)); });
  method(r, "Curve", "forward_rate", {"float", "float", "string"}, "float", [](void *p, Args &a) { return Value::real(curve_forward_rate(self<Curve>(p), a[0].f, a[1].f, a[2].s)); });
  method(r, "Curve", "bumped", {"float"}, "Curve", [&h](void *p, Args &a) { return h.hold(h.curves, "Curve", bump_curve(self<Curve>(p), a[0].f)); });
  method(r, "Curve", "save_csv", {"string", "float", "int"}, "void", [](void *p, Args &a) {
    curve_save_csv(self<Curve>(p), a[0].s, a[1].f, as_int(a[2])); return Value::none();
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
  // Exotics: a barrier is knocked in or out when the spot crosses it (continuous monitoring, Reiner-Rubinstein closed
  // form); an Asian option pays on the average of `fixings` equally spaced observations ending at expiry.
  auto barrier = [&h](bool call, Args &a) {
    need(a[0].f > 0, "option strike must be positive");
    need(a[1].f > 0, "option maturity must be positive");
    need(a[2].f > 0, "barrier must be positive");
    const std::string &t = a[3].s;
    need(t == "up-in" || t == "up-out" || t == "down-in" || t == "down-out", "barrier type must be up-in, up-out, down-in or down-out");
    Option o; o.call = call; o.strike = a[0].f; o.years = a[1].f; o.exotic = Exotic::Barrier; o.barrier = a[2].f; o.barrier_type = t;
    return h.hold(h.options, "Option", o);
  };
  method(r, "InstrumentApi", "barrier_call", {"float", "float", "float", "string"}, "Option", [barrier](void *, Args &a) { return barrier(true, a); });
  method(r, "InstrumentApi", "barrier_put", {"float", "float", "float", "string"}, "Option", [barrier](void *, Args &a) { return barrier(false, a); });
  auto asian = [&h](bool call, Args &a) {
    need(a[0].f > 0, "option strike must be positive");
    need(a[1].f > 0, "option maturity must be positive");
    need(a[2].s == "geometric" || a[2].s == "arithmetic", "averaging must be geometric or arithmetic");
    need(a[3].i >= 1 && a[3].i <= 10000, "fixings must be 1..10000");
    Option o; o.call = call; o.strike = a[0].f; o.years = a[1].f; o.exotic = Exotic::Asian; o.averaging = a[2].s; o.fixings = as_int(a[3]);
    return h.hold(h.options, "Option", o);
  };
  method(r, "InstrumentApi", "asian_call", {"float", "float", "string", "int"}, "Option", [asian](void *, Args &a) { return asian(true, a); });
  method(r, "InstrumentApi", "asian_put", {"float", "float", "string", "int"}, "Option", [asian](void *, Args &a) { return asian(false, a); });
  method(r, "InstrumentApi", "cap", {"float", "float", "float"}, "CapFloor", [&h](void *, Args &a) {
    CapFloor c; c.cap = true; c.notional = a[0].f; c.strike = a[1].f; c.years = a[2].f;
    return h.hold(h.capfloors, "CapFloor", c);
  });
  method(r, "InstrumentApi", "floor", {"float", "float", "float"}, "CapFloor", [&h](void *, Args &a) {
    CapFloor c; c.cap = false; c.notional = a[0].f; c.strike = a[1].f; c.years = a[2].f;
    return h.hold(h.capfloors, "CapFloor", c);
  });
  method(r, "InstrumentApi", "swaption", {"Swap", "float"}, "Swaption", [&h](void *, Args &a) {
    need(a[1].f > 0, "swaption expiry must be positive");
    need(!obj<Swap>(a[0]).simple, "instrument.swaption: the underlying must be a market swap (instrument.swap), not swap_simple");
    Swaption s; s.swap = obj<Swap>(a[0]); s.expiry = a[1].f;
    return h.hold(h.swaptions, "Swaption", s);
  });
  method(r, "CapFloor", "strike", {}, "float", [](void *p, Args &) { return Value::real(self<CapFloor>(p).strike); });
  method(r, "CapFloor", "years", {}, "float", [](void *p, Args &) { return Value::real(self<CapFloor>(p).years); });
  method(r, "Swaption", "expiry", {}, "float", [](void *p, Args &) { return Value::real(self<Swaption>(p).expiry); });
  method(r, "InstrumentApi", "bond", {"float", "float", "float", "int"}, "Bond", [&h](void *, Args &a) {
    Bond b; b.face = a[0].f; b.coupon = a[1].f; b.years = a[2].f; b.freq = as_int(a[3]);
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
  method(r, "InstrumentApi", "forward", {"float", "float"}, "Option", [&h](void *, Args &a) {
    need(a[0].f > 0, "instrument.forward: delivery price must be positive");
    need(a[1].f > 0, "instrument.forward: years must be positive");
    Option o; o.exotic = Exotic::Forward; o.strike = a[0].f; o.years = a[1].f;
    return h.hold(h.options, "Option", o);
  });
  method(r, "InstrumentApi", "strategy", {}, "Strategy", [&h](void *, Args &) { return h.hold(h.strategies, "Strategy", Strategy{}); });
  method(r, "Strategy", "add", {"Option", "float"}, "Strategy", [](void *p, Args &a) {
    need(self<Strategy>(p).legs.size() < 1000, "Strategy.add: at most 1000 legs");
    self<Strategy>(p).legs.emplace_back(obj<Option>(a[0]), a[1].f); return Value::object("Strategy", p);
  });
  method(r, "Strategy", "payoff", {"float"}, "float", [](void *p, Args &a) { return Value::real(strategy_payoff(self<Strategy>(p), a[0].f)); });
  method(r, "Strategy", "profit", {"float", "float"}, "float", [](void *p, Args &a) { return Value::real(strategy_payoff(self<Strategy>(p), a[0].f) - a[1].f); });
  method(r, "Strategy", "payoffs", {"float", "float", "int"}, "FloatList", [&r](void *p, Args &a) { return make_flist(r, strategy_payoffs(self<Strategy>(p), a[0].f, a[1].f, as_int(a[2]))); });
  method(r, "Option", "payoff", {"float"}, "float", [](void *p, Args &a) { return Value::real(option_payoff(self<Option>(p), a[0].f)); });
  method(r, "Option", "path_payoff", {"FloatList"}, "float", [](void *p, Args &a) { return Value::real(path_payoff(self<Option>(p), flist(a[0]))); });
  method(r, "Option", "strike", {}, "float", [](void *p, Args &) { return Value::real(self<Option>(p).strike); });
  method(r, "Option", "years", {}, "float", [](void *p, Args &) { return Value::real(self<Option>(p).years); });
  method(r, "Bond", "years", {}, "float", [](void *p, Args &) { return Value::real(self<Bond>(p).years); });
  // Textbook swap (item 12): starts today, no fixing or settlement lag, `freq` payments a year, accrual exactly 1/freq,
  // floating rate the curve's simple forward. Valued from discount factors at exact times (see durable-memory/12).
  method(r, "InstrumentApi", "swap_simple", {"float", "float", "float", "int"}, "Swap", [&h](void *, Args &a) {
    Swap s; s.notional = a[0].f; s.rate = a[1].f; s.years = a[2].f; s.freq = as_int(a[3]); s.simple = true;
    need(s.notional > 0, "instrument.swap_simple: notional must be positive");
    need(s.freq == 1 || s.freq == 2 || s.freq == 4 || s.freq == 12, "instrument.swap_simple: payments per year must be 1, 2, 4 or 12");
    const double n = s.years * s.freq;
    need(n >= 1 && std::fabs(n - std::round(n)) < 1e-9 && s.years <= 100, "instrument.swap_simple: years must be a whole number of payment periods, up to 100 years");
    return h.hold(h.swaps, "Swap", s);
  });
  method(r, "InstrumentApi", "bond_dated", {"float", "float", "string", "string", "int"}, "Bond", [&h](void *, Args &a) {
    Bond b; b.dated = true; b.face = a[0].f; b.coupon = a[1].f; b.issue = a[2].s; b.maturity = a[3].s; b.freq = as_int(a[4]);
    parse_date(b.issue); parse_date(b.maturity);
    need(b.freq == 1 || b.freq == 2 || b.freq == 4 || b.freq == 12, "instrument.bond_dated: coupon frequency must be 1, 2, 4 or 12 per year");
    return h.hold(h.bonds, "Bond", b);
  });
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
  // Wave 5: risk-neutral Black-Scholes samples (durable-memory/13). Seeded, never the clock.
  method(r, "ModelApi", "terminal_samples", {"Model", "float", "int", "int"}, "FloatList", [&r](void *, Args &a) {
    return make_flist(r, terminal_samples(obj<Model>(a[0]), a[1].f, a[2].i, a[3].i));
  });
  method(r, "ModelApi", "sample_path", {"Model", "float", "int", "int"}, "FloatList", [&r](void *, Args &a) {
    return make_flist(r, sample_path(obj<Model>(a[0]), a[1].f, a[2].i, a[3].i));
  });
  // Explicit binomial tree with a SIMPLE rate per step (no volatility): the model of the one- and multi-period lessons.
  method(r, "ModelApi", "binomial_tree", {"float", "float", "float", "float", "int"}, "Tree", [&h](void *, Args &a) {
    need(a[0].f > 0 && a[1].f > 0 && a[2].f > 0, "model.binomial_tree: spot, up and down must be positive");
    need(a[3].f > -1.0, "model.binomial_tree: the rate per step must be above -100%");
    need(a[4].i >= 1 && a[4].i <= 1000, "model.binomial_tree: steps must be 1..1000");
    Tree t; t.spot = a[0].f; t.up = a[1].f; t.down = a[2].f; t.rate = a[3].f; t.steps = as_int(a[4]);
    return h.hold(h.trees, "Tree", t);
  });
  method(r, "Tree", "q", {}, "float", [](void *p, Args &) { return Value::real(tree_q(self<Tree>(p))); });
  method(r, "Tree", "steps", {}, "int", [](void *p, Args &) { return Value::integer(self<Tree>(p).steps); });
  method(r, "Tree", "discount", {}, "float", [](void *p, Args &) { return Value::real(1.0 / (1.0 + self<Tree>(p).rate)); });
  method(r, "Tree", "stock", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_stock(self<Tree>(p), as_int(a[0]), as_int(a[1]))); });
  method(r, "Tree", "prob", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_prob(self<Tree>(p), as_int(a[0]), as_int(a[1]))); });
  method(r, "Tree", "state_price", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_state_price(self<Tree>(p), as_int(a[0]), as_int(a[1]))); });
  method(r, "Tree", "is_arbitrage_free", {}, "bool", [](void *p, Args &) { return Value::boolean(tree_arbitrage_free(self<Tree>(p))); });
  method(r, "PricingApi", "tree_price", {"Option", "Tree"}, "TreeResult", [&h](void *, Args &a) {
    return h.hold(h.tree_results, "TreeResult", tree_price(obj<Option>(a[0]), obj<Tree>(a[1])));
  });
  method(r, "TreeResult", "price", {}, "float", [](void *p, Args &) { return Value::real(self<TreeResult>(p).value[0][0]); });
  method(r, "TreeResult", "value", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_result_value(self<TreeResult>(p), "value", as_int(a[0]), as_int(a[1]))); });
  method(r, "TreeResult", "intrinsic", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_result_value(self<TreeResult>(p), "intrinsic", as_int(a[0]), as_int(a[1]))); });
  method(r, "TreeResult", "continuation", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_result_value(self<TreeResult>(p), "continuation", as_int(a[0]), as_int(a[1]))); });
  method(r, "TreeResult", "exercised", {"int", "int"}, "bool", [](void *p, Args &a) { return Value::boolean(tree_result_value(self<TreeResult>(p), "exercised", as_int(a[0]), as_int(a[1])) != 0.0); });
  method(r, "TreeResult", "delta", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_result_delta(self<TreeResult>(p), as_int(a[0]), as_int(a[1]))); });
  method(r, "TreeResult", "gamma", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_result_gamma(self<TreeResult>(p), as_int(a[0]), as_int(a[1]))); });
  method(r, "TreeResult", "bond", {"int", "int"}, "float", [](void *p, Args &a) { return Value::real(tree_result_bond(self<TreeResult>(p), as_int(a[0]), as_int(a[1]))); });
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
    Engine e = make_engine(EngineKind::Binomial); e.steps = as_int(a[0]); return h.hold(h.engines, "Engine", e);
  });
  method(r, "PricingApi", "binomial_tree", {"string", "int"}, "Engine", [&h](void *, Args &a) {
    need(a[1].i >= 2 && a[1].i <= 20000, "pricing.binomial_tree: steps must be 2..20000");
    need(a[0].s == "crr" || a[0].s == "jr" || a[0].s == "tian" || a[0].s == "lr", "pricing.binomial_tree: tree must be crr, jr, tian or lr");
    Engine e = make_engine(EngineKind::Binomial); e.tree = a[0].s; e.steps = as_int(a[1]); return h.hold(h.engines, "Engine", e);
  });
  method(r, "PricingApi", "finite_diff", {"int", "int"}, "Engine", [&h](void *, Args &a) {
    need(a[0].i >= 2 && a[0].i <= 100000 && a[1].i >= 10 && a[1].i <= 100000, "pricing.finite_diff: need 2..100000 time steps and 10..100000 grid points");
    Engine e = make_engine(EngineKind::FiniteDifference); e.steps = as_int(a[0]); e.grid = as_int(a[1]); return h.hold(h.engines, "Engine", e);
  });
  method(r, "PricingApi", "monte_carlo", {"int", "int"}, "Engine", [&h](void *, Args &a) {
    need(a[0].i >= 16 && a[0].i <= 100000000, "pricing.monte_carlo: paths must be 16..100000000");
    need(a[1].i >= 1, "pricing.monte_carlo: seed must be at least 1 (0 would mean a clock-based seed)");
    Engine e = make_engine(EngineKind::MonteCarlo); e.paths = a[0].i; e.seed = a[1].i; return h.hold(h.engines, "Engine", e);
  });
  // Wave 5. Plain Monte Carlo: no antithetic variate and, for an arithmetic Asian, no control variate.
  auto mc_args = [](const char *who, Args &a) {
    need(a[0].i >= 1 && a[0].i <= mc_max_paths, "monte carlo: paths must be 1.." + std::to_string(mc_max_paths) + ", got " + std::to_string(a[0].i));
    need(a[0].i >= 2, "monte carlo: the standard error needs at least 2 paths, got " + std::to_string(a[0].i));
    need(a[1].i >= 1 && a[1].i <= 2147483647LL, std::string(who) + ": seed must be 1..2147483647 (0 would mean a clock-based seed), got " + std::to_string(a[1].i));
  };
  method(r, "PricingApi", "monte_carlo_plain", {"int", "int"}, "Engine", [&h, mc_args](void *, Args &a) {
    mc_args("pricing.monte_carlo_plain", a);
    Engine e = make_engine(EngineKind::MonteCarlo); e.paths = a[0].i; e.seed = a[1].i; e.plain = true; return h.hold(h.engines, "Engine", e);
  });
  // Barrier options by simulation on `steps` equal time steps, monitored at the simulated points only (plain paths).
  method(r, "PricingApi", "monte_carlo_steps", {"int", "int", "int"}, "Engine", [&h, mc_args](void *, Args &a) {
    mc_args("pricing.monte_carlo_steps", a);
    need(a[2].i >= 1 && a[2].i <= 1000, "pricing.monte_carlo_steps: steps must be 1..1000, got " + std::to_string(a[2].i));
    need(a[0].i * a[2].i <= mc_max_paths, "monte carlo: paths times steps must be at most " + std::to_string(mc_max_paths) + ", got " +
                                              std::to_string(a[0].i) + " x " + std::to_string(a[2].i) + " = " + std::to_string(a[0].i * a[2].i));
    Engine e = make_engine(EngineKind::MonteCarlo); e.paths = a[0].i; e.seed = a[1].i; e.steps = as_int(a[2]); e.plain = true; e.stepped = true;
    return h.hold(h.engines, "Engine", e);
  });
  method(r, "PricingApi", "bs_d1", {"Option", "Model"}, "float", [&h](void *, Args &a) { return Value::real(bs_d1(h.clock, obj<Option>(a[0]), obj<Model>(a[1]))); });
  method(r, "PricingApi", "bs_d2", {"Option", "Model"}, "float", [&h](void *, Args &a) { return Value::real(bs_d2(h.clock, obj<Option>(a[0]), obj<Model>(a[1]))); });
  method(r, "PricingApi", "black76", {"string", "float", "float", "float", "float", "float"}, "float", [](void *, Args &a) {
    return Value::real(black76(a[0].s, a[1].f, a[2].f, a[3].f, a[4].f, a[5].f));
  });
  method(r, "PricingApi", "bachelier", {"string", "float", "float", "float", "float", "float"}, "float", [](void *, Args &a) {
    return Value::real(bachelier(a[0].s, a[1].f, a[2].f, a[3].f, a[4].f, a[5].f));
  });
  method(r, "PricingApi", "capfloor_caplets", {"CapFloor", "Curve", "float"}, "FloatList", [&h, &r](void *, Args &a) {
    return make_flist(r, capfloor_caplets(h.clock, obj<CapFloor>(a[0]), obj<Curve>(a[1]), a[2].f));
  });
  method(r, "PricingApi", "capfloor_forwards", {"CapFloor", "Curve"}, "FloatList", [&h, &r](void *, Args &a) {
    return make_flist(r, capfloor_forwards(h.clock, obj<CapFloor>(a[0]), obj<Curve>(a[1])));
  });
  method(r, "PricingApi", "capfloor_swap_npv", {"CapFloor", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(capfloor_swap_npv(h.clock, obj<CapFloor>(a[0]), obj<Curve>(a[1]))); });
  method(r, "PricingApi", "swaption_annuity", {"Swaption", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(swaption_annuity(h.clock, obj<Swaption>(a[0]), obj<Curve>(a[1]))); });
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
  method(r, "PricingApi", "npv", {"Curve", "FloatList", "FloatList"}, "float", [](void *, Args &a) {
    return Value::real(npv(obj<Curve>(a[0]), finite_list(a[1], "pricing.npv"), finite_list(a[2], "pricing.npv")));
  });
  method(r, "PricingApi", "pv_terms", {"Curve", "FloatList", "FloatList"}, "FloatList", [&r](void *, Args &a) {
    return make_flist(r, pv_terms(obj<Curve>(a[0]), finite_list(a[1], "pricing.pv_terms"), finite_list(a[2], "pricing.pv_terms")));
  });
  method(r, "PricingApi", "annuity_pv", {"float", "float", "int"}, "float", [](void *, Args &a) { return Value::real(annuity_pv(a[0].f, a[1].f, a[2].i)); });
  method(r, "PricingApi", "annuity_fv", {"float", "float", "int"}, "float", [](void *, Args &a) { return Value::real(annuity_fv(a[0].f, a[1].f, a[2].i)); });
  method(r, "PricingApi", "loan_payment", {"float", "float", "int"}, "float", [](void *, Args &a) { return Value::real(loan_payment(a[0].f, a[1].f, a[2].i)); });
  method(r, "PricingApi", "amortization", {"float", "float", "int", "string"}, "FloatList", [&r](void *, Args &a) {
    return make_flist(r, amortization(a[0].f, a[1].f, a[2].i, a[3].s));
  });
  method(r, "PricingApi", "perpetuity", {"float", "float", "float"}, "float", [](void *, Args &a) { return Value::real(perpetuity(a[0].f, a[1].f, a[2].f)); });
  method(r, "PricingApi", "dcf_two_stage", {"float", "FloatList", "float"}, "float", [](void *, Args &a) {
    return Value::real(dcf_two_stage(a[0].f, finite_list(a[1], "pricing.dcf_two_stage"), a[2].f));
  });
  method(r, "PricingApi", "forward_price", {"float", "Curve", "float", "float", "float"}, "float", [](void *, Args &a) {
    return Value::real(forward_price(obj<Curve>(a[1]), a[0].f, a[2].f, a[3].f, a[4].f));
  });
  method(r, "PricingApi", "swap_fixed_leg_npv", {"Swap", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(swap_fixed_leg_npv(h.clock, obj<Swap>(a[0]), obj<Curve>(a[1]))); });
  method(r, "PricingApi", "swap_float_leg_npv", {"Swap", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(swap_float_leg_npv(h.clock, obj<Swap>(a[0]), obj<Curve>(a[1]))); });
  method(r, "PricingApi", "swap_flows", {"Swap", "Curve", "string"}, "FloatList", [&h, &r](void *, Args &a) {
    return make_flist(r, swap_flows(h.clock, obj<Swap>(a[0]), obj<Curve>(a[1]), a[2].s));
  });
  method(r, "PricingApi", "bond_cashflow_times", {"Bond"}, "FloatList", [&h, &r](void *, Args &a) { return make_flist(r, bond_cashflow_times(h.clock, obj<Bond>(a[0]))); });
  method(r, "PricingApi", "bond_cashflow_amounts", {"Bond"}, "FloatList", [&h, &r](void *, Args &a) { return make_flist(r, bond_cashflow_amounts(h.clock, obj<Bond>(a[0]))); });
  method(r, "PricingApi", "par_yield", {"Curve", "float", "int"}, "float", [](void *, Args &a) { return Value::real(par_yield(obj<Curve>(a[0]), a[1].f, as_int(a[2]))); });
  method(r, "PricingApi", "capfloor_npv", {"CapFloor", "Curve", "float"}, "float", [&h](void *, Args &a) { return Value::real(capfloor_npv(h.clock, obj<CapFloor>(a[0]), obj<Curve>(a[1]), a[2].f)); });
  method(r, "PricingApi", "capfloor_implied_vol", {"CapFloor", "Curve", "float"}, "float", [&h](void *, Args &a) { return Value::real(capfloor_implied_vol(h.clock, obj<CapFloor>(a[0]), obj<Curve>(a[1]), a[2].f)); });
  method(r, "PricingApi", "capfloor_atm_rate", {"CapFloor", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(capfloor_atm_rate(h.clock, obj<CapFloor>(a[0]), obj<Curve>(a[1]))); });
  method(r, "PricingApi", "swaption_npv", {"Swaption", "Curve", "float"}, "float", [&h](void *, Args &a) { return Value::real(swaption_npv(h.clock, obj<Swaption>(a[0]), obj<Curve>(a[1]), a[2].f)); });
  method(r, "PricingApi", "swaption_implied_vol", {"Swaption", "Curve", "float"}, "float", [&h](void *, Args &a) { return Value::real(swaption_implied_vol(h.clock, obj<Swaption>(a[0]), obj<Curve>(a[1]), a[2].f)); });
  method(r, "PricingApi", "swaption_forward_rate", {"Swaption", "Curve"}, "float", [&h](void *, Args &a) { return Value::real(swaption_forward_rate(h.clock, obj<Swaption>(a[0]), obj<Curve>(a[1]))); });

  method(r, "Engine", "name", {}, "string", [](void *p, Args &) {
    static const char *n[] = {"analytic", "binomial", "finite_diff", "monte_carlo", "baw"};
    const Engine &e = self<Engine>(p);
    if (e.kind == EngineKind::MonteCarlo && e.stepped) return Value::string("monte_carlo_steps");
    if (e.kind == EngineKind::MonteCarlo && e.plain) return Value::string("monte_carlo_plain");
    return Value::string(n[static_cast<int>(e.kind)]);
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
    save_profile(h.clock, obj<Option>(a[0]), obj<Model>(a[1]), obj<Engine>(a[2]), a[3].f, a[4].f, as_int(a[5]), a[6].s); return Value::none();
  });
  method(r, "RiskApi", "bond_dv01", {"Bond", "Curve"}, "float", [&h](void *, Args &a) {
    Curve up = bump_curve(obj<Curve>(a[1]), 1e-4), dn = bump_curve(obj<Curve>(a[1]), -1e-4);
    return Value::real((bond_npv(h.clock, obj<Bond>(a[0]), dn) - bond_npv(h.clock, obj<Bond>(a[0]), up)) / 2.0);
  });
  // Textbook yield DV01: modified duration x dirty value x 1 bp, in currency for the bond's face (durable-memory/13).
  method(r, "RiskApi", "bond_dv01_yield", {"Bond", "float"}, "float", [&h](void *, Args &a) { return Value::real(bond_dv01_yield(h.clock, obj<Bond>(a[0]), a[1].f)); });
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

  // Empirical risk: a file of P/L or return samples (historical or simulated) -> quantiles, VaR, expected shortfall.
  method(r, "RiskApi", "mark_to_market", {"float", "FloatList"}, "FloatList", [&r](void *, Args &a) {
    return make_flist(r, mark_to_market(a[0].f, finite_list(a[1], "risk.mark_to_market")));
  });
  method(r, "RiskApi", "portfolio_return", {"FloatList", "FloatList"}, "float", [](void *, Args &a) {
    return Value::real(portfolio_return(finite_list(a[0], "risk.portfolio_return"), finite_list(a[1], "risk.portfolio_return")));
  });
  method(r, "RiskApi", "portfolio_variance", {"FloatList", "FloatList", "FloatList"}, "float", [](void *, Args &a) {
    return Value::real(portfolio_variance(finite_list(a[0], "risk.portfolio_variance"), finite_list(a[1], "risk.portfolio_variance"), finite_list(a[2], "risk.portfolio_variance")));
  });
  method(r, "RiskApi", "portfolio_vol", {"FloatList", "FloatList", "FloatList"}, "float", [](void *, Args &a) {
    return Value::real(std::sqrt(portfolio_variance(finite_list(a[0], "risk.portfolio_vol"), finite_list(a[1], "risk.portfolio_vol"), finite_list(a[2], "risk.portfolio_vol"))));
  });
  method(r, "RiskApi", "min_variance_weights", {"FloatList", "FloatList"}, "FloatList", [&r](void *, Args &a) {
    return make_flist(r, min_variance_weights(finite_list(a[0], "risk.min_variance_weights"), finite_list(a[1], "risk.min_variance_weights")));
  });
  method(r, "RiskApi", "tangency_weights", {"FloatList", "FloatList", "FloatList", "float"}, "FloatList", [&r](void *, Args &a) {
    return make_flist(r, tangency_weights(finite_list(a[0], "risk.tangency_weights"), finite_list(a[1], "risk.tangency_weights"), finite_list(a[2], "risk.tangency_weights"), a[3].f));
  });
  method(r, "RiskApi", "loss_distribution", {"FloatList", "FloatList"}, "LossDist", [&h](void *, Args &a) {
    return h.hold(h.loss_dists, "LossDist", loss_distribution(finite_list(a[0], "risk.loss_distribution"), finite_list(a[1], "risk.loss_distribution")));
  });
  method(r, "LossDist", "mean", {}, "float", [](void *p, Args &) { return Value::real(loss_mean(self<LossDist>(p))); });
  method(r, "LossDist", "cdf", {"float"}, "float", [](void *p, Args &a) { return Value::real(loss_cdf(self<LossDist>(p), a[0].f)); });
  method(r, "LossDist", "var", {"float"}, "float", [](void *p, Args &a) { return Value::real(loss_var(self<LossDist>(p), a[0].f)); });
  method(r, "LossDist", "es", {"float"}, "float", [](void *p, Args &a) { return Value::real(loss_es(self<LossDist>(p), a[0].f)); });
  method(r, "RiskApi", "series", {"string"}, "Series", [&h](void *, Args &a) { return h.hold(h.series, "Series", load_series(a[0].s)); });
  method(r, "Series", "size", {}, "int", [](void *p, Args &) { return Value::integer((int64_t)self<Series>(p).v.size()); });
  method(r, "Series", "get", {"int"}, "float", [](void *p, Args &a) {
    const auto &v = self<Series>(p).v;
    need(a[0].i >= 0 && a[0].i < (int64_t)v.size(), "Series.get: index " + std::to_string(a[0].i) + " outside 0.." + std::to_string(v.size() - 1));
    return Value::real(v[a[0].i]);
  });
  method(r, "Series", "mean", {}, "float", [](void *p, Args &) {
    const auto &v = self<Series>(p).v; double s = 0; for (double x : v) s += x; return Value::real(s / v.size());
  });
  method(r, "Series", "std", {}, "float", [](void *p, Args &) {
    const auto &v = self<Series>(p).v;
    need(v.size() >= 2, "Series.std: need at least 2 values");
    double m = 0; for (double x : v) m += x; m /= v.size();
    double s = 0; for (double x : v) s += (x - m) * (x - m);
    return Value::real(std::sqrt(s / (v.size() - 1)));
  });
  method(r, "Series", "min", {}, "float", [](void *p, Args &) { const auto &v = self<Series>(p).v; return Value::real(*std::min_element(v.begin(), v.end())); });
  method(r, "Series", "max", {}, "float", [](void *p, Args &) { const auto &v = self<Series>(p).v; return Value::real(*std::max_element(v.begin(), v.end())); });
  method(r, "Series", "quantile", {"float"}, "float", [](void *p, Args &a) { return Value::real(series_quantile(self<Series>(p), a[0].f)); });
  method(r, "Series", "var", {"float"}, "float", [](void *p, Args &a) { return Value::real(series_var(self<Series>(p), a[0].f)); });
  method(r, "Series", "es", {"float"}, "float", [](void *p, Args &a) { return Value::real(series_es(self<Series>(p), a[0].f)); });

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

  method(r, "HestonFit", "errors", {}, "FloatList", [&r](void *p, Args &) {
    need(self<HestonFit>(p).evaluations >= 0, "HestonFit.errors: call fit() first");
    return make_flist(r, self<HestonFit>(p).errors);
  });
  method(r, "HestonFit", "evaluations", {}, "int", [](void *p, Args &) {
    need(self<HestonFit>(p).evaluations >= 0, "HestonFit.evaluations: call fit() first");
    return Value::integer(self<HestonFit>(p).evaluations);
  });
  method(r, "HestonFit", "reason", {}, "string", [](void *p, Args &) {
    need(self<HestonFit>(p).evaluations >= 0, "HestonFit.reason: call fit() first");
    return Value::string(self<HestonFit>(p).reason);
  });
  method(r, "CalibrationApi", "smile", {"Model", "FloatList", "float"}, "FloatList", [&h, &r](void *, Args &a) {
    return make_flist(r, smile(h.clock, obj<Model>(a[0]), finite_list(a[1], "calibration.smile"), a[2].f));
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
  method(r, "LmfApi", "round", {"float", "int"}, "float", [](void *, Args &a) { return Value::real(round_decimal(a[0].f, as_int(a[1]))); });
  method(r, "LmfApi", "money", {"float"}, "string", [](void *, Args &a) { return Value::string(money_text(a[0].f)); });
  method(r, "LmfApi", "fraction", {"float", "int"}, "string", [](void *, Args &a) { return Value::string(fraction_text(a[0].f, a[1].i)); });
  method(r, "LmfApi", "exp", {"float"}, "float", [](void *, Args &a) { return Value::real(math_exp(a[0].f)); });
  method(r, "LmfApi", "log", {"float"}, "float", [](void *, Args &a) { return Value::real(math_log(a[0].f)); });
  method(r, "LmfApi", "norm_cdf", {"float"}, "float", [](void *, Args &a) { return Value::real(norm_cdf(a[0].f)); });
  method(r, "LmfApi", "norm_pdf", {"float"}, "float", [](void *, Args &a) { return Value::real(norm_pdf(a[0].f)); });
  method(r, "LmfApi", "norm_inv", {"float"}, "float", [](void *, Args &a) { return Value::real(norm_inv(a[0].f)); });
  method(r, "LmfApi", "print", {"string"}, "void", [](void *, Args &a) { std::cout << a[0].s << std::endl; return Value::none(); });
  method(r, "LmfApi", "version", {}, "string", [](void *, Args &) { return Value::string("lmfinance " LMFINANCE_VERSION); });
}

const char *usage = "usage: lmfinance --version | --notices | --lmcode-schema [--json] | --lmcode-signatures | [--dry-run] --lmcode FILE [FILE ...]\n"
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
  if (argc >= 2 && a1 == "--lmcode-schema") {
    if (argc == 2) { for (const auto &s : reg.signatures()) std::cout << s << "\n"; return std::cout ? 0 : 4; }
    if (argc == 3 && std::string(argv[2]) == "--json") {
      std::cout.write(reinterpret_cast<const char *>(lmfinance::embedded_schema), (std::streamsize)lmfinance::embedded_schema_size);
      return std::cout ? 0 : 4;
    }
    std::cerr << usage; return 2;
  }
  if (argc == 2 && a1 == "--lmcode-signatures") { for (const auto &s : reg.signatures()) std::cout << s << "\n"; return 0; }
  int first = 2;
  bool dry = argc >= 2 && a1 == "--dry-run";  // accepted for symmetry: every result lands in files/tables anyway
  if (dry) first = 3;
  // --lmcode: one invocation envelope (lmcode-cpp InvocationResults). Exit 0 success,
  // 2 cli, 3 load/run, 4 application (APP_FAIL) or output failure.
  if (argc <= first || std::string(argv[first - 1]) != "--lmcode") {
    std::cerr << usage;
    return dslvm::failure_frame("cli", dslvm::Error("CLI_USAGE", "expected [--dry-run] --lmcode FILE [FILE ...]"));
  }
  std::vector<std::string> files;
  for (int i = first; i < argc; i++) {
    if (std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0) {
      std::cerr << usage;
      return dslvm::failure_frame("cli", dslvm::Error("CLI_USAGE", "invalid program file argument"));
    }
    files.push_back(argv[i]);
  }
  if (!membudget::configure("lmfinance", "LMFINANCE_MAX_MEMORY_MB")) {
    std::cerr << usage;
    return dslvm::failure_frame("cli", dslvm::Error("CLI_USAGE", "invalid LMFINANCE_MAX_MEMORY_MB"));
  }
  // Dry mode is invocation-wide: run_lmcode supplies it in LMCODE_INVOCATION_FILE metadata; the
  // standalone --dry-run flag requests the same mode when no metadata is present.
  std::optional<dslvm::InvocationResults> holder;
  try {
    if (dry && !std::getenv("LMCODE_INVOCATION_FILE")) holder.emplace(dslvm::InvocationResults::from_command(files, true));
    else holder.emplace(dslvm::InvocationResults::from_command(files));
  } catch (const dslvm::Error &e) {
    std::cerr << usage;
    return dslvm::failure_frame("cli", e);
  }
  auto &results = *holder;
  if (dry && !results.dry()) {
    std::cerr << usage;
    return dslvm::failure_frame("cli", dslvm::Error("CLI_USAGE", "--dry-run contradicts real-mode invocation metadata"));
  }
  const std::string schema(reinterpret_cast<const char *>(lmfinance::embedded_schema), lmfinance::embedded_schema_size);
  dslvm::Context context;
  for (const char *n : {"market", "instrument", "model", "pricing", "risk", "calibration", "lmf"}) context.set(n, &host);
  // A program that exhausted the memory budget still needs a failure frame, and the market state it
  // built stays allocated: this reserve is released first, so recording the failure cannot throw.
  auto reserve = std::make_unique<char[]>(1 << 20);
  dslvm::ResultContract contract;
  // Load and verify every file first: a bad later file must not leave earlier tables behind.
  std::vector<std::pair<size_t, dslvm::Program>> programs;
  try { contract = dslvm::ResultContract::from_schema(schema); }
  catch (const dslvm::Error &e) { reserve.reset(); results.fail(std::nullopt, "load", e); return results.write_frame(std::cout); }
  for (const auto &part : results.registrations()) {
    try { programs.emplace_back(part.index, dslvm::Program::load(dslvm::read_file(part.file), schema, reg)); }
    catch (const dslvm::Error &e) { reserve.reset(); results.fail(part.index, "load", e); return results.write_frame(std::cout); }
    catch (const std::exception &e) { reserve.reset(); results.fail(part.index, "load", dslvm::Error(e.what())); return results.write_frame(std::cout); }
  }
  for (auto &[index, program] : programs) {
    try {
      results.select(index, reg, contract);
      // A dry invocation validates and loads every program but runs none: programs write tables and
      // price books, and a valuation has no effect-free mode.
      if (!results.dry()) program.run(context, reg);
      results.complete(index);
    } catch (const dslvm::Error &e) {
      reserve.reset();
      std::cerr << e.what() << "\n";
      results.fail(index, e.code == "APP_FAIL" ? "app" : "run", e);
      break;
    } catch (const std::exception &e) {  // the memory budget inside the VM
      reserve.reset();
      std::cerr << e.what() << "\n";
      results.fail(index, "run", dslvm::Error(e.what()));
      break;
    }
  }
  return results.write_frame(std::cout);
}
