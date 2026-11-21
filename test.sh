#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
BUILD_DIR="${BUILD_DIR:-build}"
HOST="$BUILD_DIR/lmfinance"
UNAME_S="$(uname -s)"
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) HOST="$HOST.exe";; esac
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lmfinance-test-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
ROOT="$HERE"
# Native Windows tools (node, the host) cannot address git-bash /tmp or /d/a paths.
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) WORK="$(cd "$WORK" && pwd -W | tr '\\' '/')"; ROOT="$(pwd -W | tr '\\' '/')";; esac
pass=0
ok(){ pass=$((pass+1)); echo "ok $pass - $1"; }
fail(){ echo "FAIL: $*" >&2; exit 1; }
sha(){ if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }

GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
# shellcheck disable=SC2086
cmake -S . -B "$BUILD_DIR" $GEN -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" >"$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log" >&2; fail "build"; }
ok "build"

# A shim named lmfinance, first on PATH, execs the build under test: cli="lmfinance" blocks and
# explicit ["lmfinance", "--lmcode", "$DSLFILE"] commands bind to it. For each --lmcode call it
# records the host's own exit code and keeps a copy of every private program file (the bytecode
# header checks and the budget/dry checks, whose limits apply to the host alone, use those files).
SHIM="$(cd "$BUILD_DIR" && pwd)/test-shim"; rm -rf "$SHIM"; mkdir -p "$SHIM/runs"
HOST_ABS="$(cd "$BUILD_DIR" && pwd)/$(basename "$HOST")"
case "$UNAME_S" in
  MINGW*|MSYS*|CYGWIN*)
    # Windows spawns the host without a shell, so the shim must be a real executable.
    SHIM_W="$(cd "$SHIM" && pwd -W)"; HOST_W="$(cd "$BUILD_DIR" && pwd -W)/$(basename "$HOST")"
    "${CXX:-g++}" -std=c++17 -O1 -DHOST_PATH="\"$HOST_W\"" -DSHIM_DIR="\"$SHIM_W\"" -o "$SHIM/lmfinance.exe" tests/shim.cpp || fail "build Windows test shim";;
  *)
    cat >"$SHIM/lmfinance" <<EOF2
#!/bin/sh
[ "\${1:-}" = --lmcode ] || exec "$HOST_ABS" "\$@"
tag="\$(mktemp -d "$SHIM/runs/run.XXXXXX")"
i=0
for a in "\$@"; do case "\$a" in --*) ;; *) cp "\$a" "\$tag/p\$i"; i=\$((i+1));; esac; done
"$HOST_ABS" "\$@"; c=\$?
echo "\$c" > "\$tag/code"; echo "\$tag" > "$SHIM/last"; exit "\$c"
EOF2
    chmod +x "$SHIM/lmfinance";;
esac
# On Windows the npm lmscript must go through its .cmd shim, never a bare bash invocation.
PATH="$SHIM:$PATH"
export PATH
LMSCRIPT="$(scripts/select-compiler.sh)"
echo "compiler: $LMSCRIPT"
# run_lms FILE [lmscript flags]: run a file; its stdout goes to stdout. Returns the host's exit code
# from its last --lmcode call; 125 when no host ran (the file did not compile).
run_lms() {
  local file="$1"; shift
  rm -f "$SHIM/last"
  "$LMSCRIPT" "$@" "$file" || true
  [ -f "$SHIM/last" ] || return 125
  LAST_RUN="$(cat "$SHIM/last")"
  return "$(cat "$LAST_RUN/code")"
}
# capture NAME: load $WORK/NAME.lms in a dry invocation (nothing runs); $WORK/NAME.prog is its program file.
capture() {
  rm -f "$SHIM/last"
  "$LMSCRIPT" --dry-run "$WORK/$1.lms" >/dev/null 2>"$WORK/$1.cerr" || { cat "$WORK/$1.cerr" >&2; fail "$1 compile"; }
  [ -f "$SHIM/last" ] || fail "$1 did not reach the host"
  cp "$(cat "$SHIM/last")/p0" "$WORK/$1.prog"
}
check_v6() { # program file: bytecode v6 for the raw host schema bytes
  python3 - "$1" <<'PYV6'
from pathlib import Path
import hashlib,sys
b=Path(sys.argv[1]).read_bytes()
assert b[:6] == b'DSLB\x00\x06', 'not bytecode v6'
assert b[6:38] == hashlib.sha256(Path('schema/lmfinance.dsl.json').read_bytes()).digest(), 'schema hash differs from the raw host schema bytes'
PYV6
}
scripts/sync-sdk.sh --verify >/dev/null || fail "SDK header pin mismatch"
ok "SDK header matches sdk.lock"
scripts/sync-deps.sh --verify >/dev/null || fail "third_party pin mismatch"
ok "third_party matches deps.lock"
python3 tests/check-host-flags.py schema/lmfinance.dsl.json "$HOST" || fail "hard host flag cutover"

want_version="lmfinance $(sed -n 's/^project(lmfinance VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ "$want_version" = "lmfinance 0.1.4" ] || fail "CMakeLists.txt version is not 0.1.4 ($want_version)"
[ "$("$HOST" --version)" = "$want_version" ] || fail "--version is not exactly '$want_version'"
ok "--version prints exactly '$want_version'"
python3 scripts/gen-schema.py >/dev/null && git diff --quiet -- schema/lmfinance.dsl.json 2>/dev/null || [ ! -e .git ] || fail "schema/lmfinance.dsl.json is stale: run scripts/gen-schema.py"
ok "schema matches the registrations in cpp/main.cpp"
"$HOST" --lmcode-schema --json >"$WORK/schema.json" || fail "embedded schema print"
cmp -s "$WORK/schema.json" schema/lmfinance.dsl.json || fail "embedded schema differs from schema/lmfinance.dsl.json"
ok "--lmcode-schema --json prints byte-identical embedded schema bytes"
# The registry includes the vendored standard library (206 built-in signatures); the host schema declares only its own members.
"$HOST" --lmcode-signatures >"$WORK/registry-signatures.txt"
"$LMSCRIPT" --lmcode-signatures cpp/std/lmcode-std.schema.json >"$WORK/standard-signatures.txt"
python3 - "$WORK" <<'PYHOST' || fail "standard signature surface differs from the vendored std schema"
from pathlib import Path
import sys
work = Path(sys.argv[1])
registry = set((work / "registry-signatures.txt").read_text().splitlines())
standard = set((work / "standard-signatures.txt").read_text().splitlines())
assert len(standard) == 206, len(standard)
assert standard <= registry, "registry lacks standard signatures"
(work / "host-signatures.txt").write_bytes("".join(line + "\n" for line in sorted(registry - standard)).encode("utf-8"))
PYHOST
diff -u <("$LMSCRIPT" --lmcode-signatures schema/lmfinance.dsl.json --without-std) "$WORK/host-signatures.txt" || fail "host/compiler signatures differ"
ok "host registry matches compiler signatures (206 standard built-ins excluded)"
"$HOST" --notices | grep -q "QuantLib" || fail "--notices missing third-party text"
"$HOST" --notices >"$WORK/notices.txt" || fail "--notices"
for n in "Makoto Matsumoto" "University of Chicago" "Boost C++ Libraries"; do
  grep -q "$n" "$WORK/notices.txt" || fail "--notices missing the $n notice"
done
ok "--notices prints the third-party license texts"

case "$UNAME_S" in
  Linux)
    python3 scripts/test-linux-linkage.py || fail "Linux linkage checker regressions"
    python3 scripts/check-linux-linkage.py "$HOST" || fail "Linux dependency allowlist or glibc baseline"
    ok "only dynamic system glibc is needed; readelf audits the glibc 2.35 ABI baseline";;
  Darwin) bad="$(otool -L "$HOST" | tail -n +2 | grep -v -E '/usr/lib/(libSystem|libc\+\+|libresolv)' || true)"
          [ -z "$bad" ] || fail "non-system dylib dependencies: $bad"; ok "only system libraries linked";;
  MINGW*|MSYS*|CYGWIN*)
    if command -v objdump >/dev/null 2>&1; then
      bad="$(objdump -p "$HOST" | grep 'DLL Name' | grep -i -E 'libstdc|libgcc|winpthread|libgomp' || true)"
      [ -z "$bad" ] || fail "MinGW runtime DLLs are imported: $bad"
      ok "no MinGW runtime DLL imports"
    else ok "static-link check skipped (no objdump)"; fi;;
  *) ok "static-link check skipped on $UNAME_S";;
esac

# Programs: every test and example runs through lmscript (which hands the host a private program
# file over the invocation envelope); each asserts its own numbers, and the shimmed copy proves the
# file the host received is bytecode v6 for this schema.
mkdir -p build/examples
n=0
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  rm -f "$SHIM/last"
  "$LMSCRIPT" "$src" >"$WORK/$name.log" 2>&1 || { cat "$WORK/$name.log" >&2; fail "$name run"; }
  [ -f "$SHIM/last" ] || { cat "$WORK/$name.log" >&2; fail "$name did not reach the host"; }
  run_dir="$(cat "$SHIM/last")"
  for program in "$run_dir"/p*; do check_v6 "$program" || fail "$name is not bytecode v6 for the host schema"; done
  n=$((n+1))
done
ok "$n programs run under the invocation envelope, pass their own assertions, and are bytecode v6 for the host schema"

# Determinism: the CSV files the examples write are identical on rerun.
for f in build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/before.txt"
for src in examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$LMSCRIPT" "$src" >"$WORK/$name.rerun.log" 2>&1 || { cat "$WORK/$name.rerun.log" >&2; fail "$name rerun"; }
done
for f in build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/after.txt"
cmp -s "$WORK/before.txt" "$WORK/after.txt" || fail "result files differ on rerun"
ok "all $(wc -l <"$WORK/after.txt" | tr -d ' ') result files are byte-identical on rerun"

# Standard library built into lmscript: one function from each family runs on this host (file I/O in $WORK).
cat >"$WORK/std.lms" <<LMS
p = _lmcode_ cli="lmfinance";
lmf.print("string=" + str_upper("abc") + ":" + str(str_len("hello")));
lmf.print("math=" + str(math_max(3, 9)));
let xs = int_list().add(4).add(7);
lmf.print("collections=" + str(xs.len()) + ":" + str(xs.at(1)));
lmf.print("split=" + str_split("a,b,c", ",").join("-"));
let path = "$WORK/std.txt";
file_write(path, "line one\n");
file_append(path, "line two\n");
lmf.print("file=" + str(file_exists(path)) + ":" + str(str_len(file_read(path))));
end _lmcode_
r = run_lmcode(p);
println(json_stringify(r));
LMS
run_lms "$WORK/std.lms" >"$WORK/std.raw" 2>"$WORK/std.err" || { code=$?; cat "$WORK/std.err"; [ "$code" = 125 ] && fail "std program compile"; fail "std program run"; }
python3 - "$WORK/std.raw" <<'PYRUN' || fail "std program emitted unexpected output"
from pathlib import Path
import sys
lines = Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
expected = [
    "string=ABC:5",
    "math=9",
    "collections=2:7",
    "split=a-b-c",
    "file=true:18",
    '{"ok":true,"result":null}',
]
assert lines == expected, lines
PYRUN
ok "standard library runs end to end on the host: string, math, collections, file"

# Error paths: each snippet must fail the run with exit 3 and a message naming the problem.
printf '1 2 3\n' >"$WORK/pnl.csv"
expect_error() { # name, snippet, expected message fragment
  cat >"$WORK/$1.lms" <<LMS
p = _lmcode_ cli="lmfinance";
$2
end _lmcode_
r = run_lmcode(p);
println(json_stringify(r));
LMS
  set +e; out="$(run_lms "$WORK/$1.lms" 2>"$WORK/$1.cerr")"; code=$?; set -e
  [ "$code" != 125 ] || { cat "$WORK/$1.cerr" >&2; fail "$1 compile"; }
  [ "$code" = 3 ] || fail "$1: exit code $code, expected 3 ($out)"
  echo "$out" | grep -q "$3" || fail "$1: message lacks '$3': $out"
}
expect_error baddate 'market.set_date("2024-13-01");' "out of range"
expect_error nodate 'market.set_date("tomorrow");' "YYYY-MM-DD"
expect_error negvol 'let m = model.black_scholes(100.0, 0.05, 0.0, -0.2);' "volatility must be positive"
expect_error hestonrho 'let m = model.heston(100.0, 0.05, 0.0, 0.04, 1.5, 0.04, 0.5, 1.5);' "rho must be inside"
expect_error analyticamerican 'let r = pricing.price(instrument.american_put(100.0, 1.0), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.analytic());' "European options only"
expect_error hestonengine 'let r = pricing.price(instrument.european_call(100.0, 1.0), model.heston(100.0, 0.05, 0.0, 0.04, 1.5, 0.04, 0.5, -0.5), pricing.binomial(100));' "analytic engine only"
expect_error mcseed 'let e = pricing.monte_carlo(1000, 0);' "seed must be at least 1"
expect_error mcgreek 'let r = pricing.price(instrument.european_call(100.0, 1.0), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.monte_carlo(1000, 3)); let d = r.delta();' "does not provide"
expect_error badtree 'let e = pricing.binomial_tree("xyz", 100);' "tree must be"
expect_error badtenor 'let c = market.bootstrap_curve().deposit("soon", 0.03).build();' "bad tenor"
expect_error emptycurve 'let c = market.zero_curve().build();' "at least one zero"
expect_error wrongbuilder 'let b = market.zero_curve().deposit("3M", 0.03);' "use market.bootstrap_curve"
expect_error ivrange 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let v = calibration.implied_vol(instrument.european_call(100.0, 1.0), m, pricing.analytic(), 150.0);' "outside the range"
expect_error bondmonths 'let b = instrument.bond(100.0, 0.05, 2.04, 1); let y = pricing.bond_price_from_yield(b, 0.05);' "whole number of months"
expect_error fewquotes 'let f = calibration.heston(100.0, 0.03, 0.0).quote(100.0, 1.0, 0.2); let m = f.fit();' "at least 5 quotes"
expect_error assertfail 'lmf.assert_close(1.0, 2.0, 0.1);' "assert_close failed"
expect_error checkfail 'lmf.check(1.0 > 2.0, "one is not greater than two");' "check failed: one is not greater"
expect_error nanrate 'let m = model.black_scholes(100.0, sqrt(-1.0), 0.0, 0.2);' "argument 2 must be finite"
expect_error nancurve 'let c = market.flat_curve(sqrt(-1.0));' "must be finite"
expect_error subday 'let r = pricing.price(instrument.european_call(50.0, 0.001), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.analytic());' "shorter than one day"
expect_error badguess 'let f = calibration.heston(100.0, 0.03, 0.0).guess(0.04, 1.0, 0.04, 0.5, 3.0);' "rho must be inside"
expect_error badbarrier 'let o = instrument.barrier_call(100.0, 1.0, 90.0, "sideways");' "barrier type must be"
expect_error barrierengine 'let r = pricing.price(instrument.barrier_put(100.0, 1.0, 90.0, "down-in"), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.binomial(100));' "analytic engine only"
expect_error asianengine 'let r = pricing.price(instrument.asian_call(100.0, 1.0, "arithmetic", 12), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.analytic());' "monte_carlo engine"
expect_error asianfix 'let r = pricing.price(instrument.asian_call(100.0, 0.01, "geometric", 12), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.analytic());' "same day"
expect_error capmonths 'let v = pricing.capfloor_npv(instrument.cap(1000000.0, 0.04, 2.3), market.flat_curve(0.04), 0.2);' "half years"
expect_error swaptionexp 'let v = pricing.swaption_npv(instrument.swaption(instrument.swap(1000000.0, 0.04, 5.0), 0.3), market.flat_curve(0.04), 0.2);' "whole number of months"
expect_error noseries 'let s = risk.series("does-not-exist.csv");' "cannot open"
expect_error seriesidx 'let s = risk.series("'"$WORK"'/pnl.csv"); let x = s.get(5);' "outside"
expect_error badcompound 'let c = market.flat_curve_compounded(0.05, "weekly");' "compounding must be"
expect_error npvlengths 'let t = float_list().add(1.0); let a = float_list(); let v = pricing.npv(market.flat_curve(0.05), t, a);' "equally long"
expect_error perpgrowth 'let v = pricing.perpetuity(100.0, 0.04, 0.05);' "must exceed the growth"
expect_error treearb 'let r = pricing.tree_price(instrument.european_call(100.0, 1.0), model.binomial_tree(100.0, 1.2, 0.9, 0.25, 1));' "not arbitrage-free"
expect_error treenode 'let t = model.binomial_tree(100.0, 1.2, 0.9, 0.05, 2); let s = t.stock(2, 3);' "outside 0..2"
expect_error weightsum 'let w = float_list().add(0.5).add(0.6); let v = float_list().add(0.1).add(0.1); let c = float_list().add(1.0).add(0.0).add(0.0).add(1.0); let x = risk.portfolio_variance(w, v, c);' "must sum to 1"
expect_error corrasym 'let w = float_list().add(0.5).add(0.5); let v = float_list().add(0.1).add(0.1); let c = float_list().add(1.0).add(0.2).add(0.3).add(1.0); let x = risk.portfolio_variance(w, v, c);' "must be symmetric"
expect_error lossprob 'let d = risk.loss_distribution(float_list().add(1.0).add(2.0), float_list().add(0.5).add(0.6));' "must sum to 1"
expect_error logneg 'let x = lmf.log(0.0 - 1.0);' "must be positive"
expect_error forwardprice 'let p = pricing.price(instrument.forward(100.0, 1.0), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.analytic());' "no option price"
expect_error simpleswaption 'let s = instrument.swaption(instrument.swap_simple(100.0, 0.05, 3.0, 1), 1.0);' "not swap_simple"
expect_error amortcolumn 'let a = pricing.amortization(1000.0, 0.1, 3, "total");' "column must be"
ok "errors stop the program with exit 3 and a clear message (40 cases)"
# Wave 5 additions (durable-memory/13): every new guard names the function and the offending value.
expect_error norminv 'let x = lmf.norm_inv(1.0);' "lmf.norm_inv: probability must be inside (0, 1), got 1"
expect_error d1american 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = pricing.bs_d1(instrument.american_put(100.0, 1.0), m);' "pricing.bs_d1: needs a plain European option (this one is American)"
expect_error d2heston 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = pricing.bs_d2(c, model.heston(100.0, 0.05, 0.0, 0.04, 1.5, 0.04, 0.3, 0.0 - 0.7));' "pricing.bs_d2: needs a Black-Scholes model"
expect_error mcceiling 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let r = pricing.price(c, m, pricing.monte_carlo(4000001, 7));' "monte carlo: paths must be 1..4000000, got 4000001"
expect_error mcplainceiling 'let e = pricing.monte_carlo_plain(5000000, 7);' "monte carlo: paths must be 1..4000000, got 5000000"
expect_error mcplainone 'let e = pricing.monte_carlo_plain(1, 7);' "needs at least 2 paths, got 1"
expect_error mcplainseed 'let e = pricing.monte_carlo_plain(1000, 0);' "pricing.monte_carlo_plain: seed must be 1..2147483647"
expect_error mcasianwork 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let r = pricing.price(instrument.asian_call(100.0, 1.0, "arithmetic", 12), m, pricing.monte_carlo(400000, 7));' "monte carlo: paths times fixings must be at most 4000000, got 400000 x 12 = 4800000"
expect_error mcstepswork 'let e = pricing.monte_carlo_steps(100000, 7, 100);' "monte carlo: paths times steps must be at most 4000000, got 100000 x 100 = 10000000"
expect_error mcstepsrange 'let e = pricing.monte_carlo_steps(1000, 7, 1001);' "pricing.monte_carlo_steps: steps must be 1..1000, got 1001"
expect_error mcstepsplain 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let r = pricing.price(c, m, pricing.monte_carlo_steps(1000, 7, 10));' "pricing.monte_carlo_steps prices barrier options"
expect_error mcstepstouched 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let r = pricing.price(instrument.barrier_call(100.0, 1.0, 90.0, "up-out"), m, pricing.monte_carlo_steps(1000, 7, 10));' "barrier touched: the spot 100 is already at or beyond the up-out barrier 90"
expect_error barrierplainmc 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let r = pricing.price(instrument.barrier_call(100.0, 1.0, 120.0, "up-out"), m, pricing.monte_carlo_plain(1000, 7));' "analytic engine only"
expect_error samplesn 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = model.terminal_samples(m, 1.0, 10001, 1);' "model.terminal_samples: n must be 1..10000, got 10001"
expect_error samplesheston 'let d = model.terminal_samples(model.heston(100.0, 0.05, 0.0, 0.04, 1.5, 0.04, 0.3, 0.0 - 0.7), 1.0, 10, 1);' "model.terminal_samples: needs a Black-Scholes model"
expect_error pathsteps 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = model.sample_path(m, 1.0, 0, 1);' "model.sample_path: steps must be 1..1000, got 0"
expect_error pathseed 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = model.sample_path(m, 1.0, 10, 0);' "model.sample_path: seed must be 1..2147483647"
expect_error pathyears 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = model.sample_path(m, 0.001, 10, 1);' "model.sample_path: 0.001 years is shorter than one day"
expect_error payoffamerican 'let d = instrument.american_put(100.0, 1.0).path_payoff(float_list().add(100.0));' "Option.path_payoff: an American option"
expect_error payoffempty 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = c.path_payoff(float_list());' "Option.path_payoff: the path is empty"
expect_error payoffneg 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = c.path_payoff(float_list().add(100.0).add(0.0 - 3.0));' "Option.path_payoff: prices must be positive, got -3 at index 1"
expect_error payoffasian1 'let d = instrument.asian_call(100.0, 1.0, "arithmetic", 3).path_payoff(float_list().add(100.0));' "at least one fixing"
expect_error b76kind 'let d = pricing.black76("straddle", 0.04, 0.04, 0.2, 1.0, 1.0);' "pricing.black76: kind must be call or put, got 'straddle'"
expect_error b76forward 'let d = pricing.black76("call", 0.0 - 0.01, 0.04, 0.2, 1.0, 1.0);' "pricing.black76: forward must be positive, got -0.01"
expect_error bachvol 'let d = pricing.bachelier("put", 0.01, 0.04, 0.0, 1.0, 1.0);' "pricing.bachelier: vol must be positive, got 0"
expect_error capletvol 'let d = pricing.capfloor_caplets(instrument.cap(1000000.0, 0.04, 5.0), market.flat_curve(0.04), 0.0);' "cap/floor volatility must be positive"
expect_error capswapyears 'let d = pricing.capfloor_swap_npv(instrument.cap(1000000.0, 0.04, 2.3), market.flat_curve(0.04));' "half years"
expect_error annuityexpiry 'let d = pricing.swaption_annuity(instrument.swaption(instrument.swap(1000000.0, 0.04, 5.0), 0.3), market.flat_curve(0.04));' "whole number of months"
expect_error smilestrike 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = calibration.smile(m, float_list().add(0.0 - 5.0), 1.0);' "calibration.smile: strikes must be positive, got -5 at index 0"
expect_error smileempty 'let m = model.black_scholes(100.0, 0.05, 0.0, 0.2); let c = instrument.european_call(100.0, 1.0); let d = calibration.smile(m, float_list(), 1.0);' "calibration.smile: needs 1..1000 strikes, got 0"
expect_error fiterrors 'let d = calibration.heston(100.0, 0.05, 0.0).errors();' "HestonFit.errors: call fit() first"
expect_error fitevals 'let d = calibration.heston(100.0, 0.05, 0.0).evaluations();' "HestonFit.evaluations: call fit() first"
expect_error fitreason 'let d = calibration.heston(100.0, 0.05, 0.0).reason();' "HestonFit.reason: call fit() first"
expect_error yieldduration 'let d = pricing.bond_duration(instrument.bond(1000.0, 0.05, 10.0, 1), 0.0 - 1.5);' "pricing.bond_duration: yield must be above -100% per coupon period (above -100% a year for 1 period a year), got -1.5"
expect_error yieldmacaulay 'let d = pricing.bond_macaulay_duration(instrument.bond(1000.0, 0.05, 10.0, 1), 0.0 - 1.0);' "pricing.bond_macaulay_duration: yield must be above -100%"
expect_error yieldconvexity 'let d = pricing.bond_convexity(instrument.bond(1000.0, 0.05, 10.0, 4), 0.0 - 4.0);' "pricing.bond_convexity: yield must be above -100% per coupon period (above -400% a year for 4 periods a year), got -4"
expect_error yieldprice 'let d = pricing.bond_price_from_yield(instrument.bond(1000.0, 0.05, 10.0, 2), 0.0 - 2.0);' "pricing.bond_price_from_yield: yield must be above -100% per coupon period (above -200% a year for 2 periods a year), got -2"
expect_error yielddv01 'let d = risk.bond_dv01_yield(instrument.zero_bond(1000.0, 5.0), 0.0 - 1.0);' "risk.bond_dv01_yield: yield must be above -100%"
ok "wave 5 guards stop the program with exit 3 and a clear message (38 cases)"

# --dry-run loads and verifies the program but runs nothing (no output files), both through
# lmscript's dry invocation metadata and through the standalone host flag.
cat >"$WORK/dry.lms" <<LMS
p = _lmcode_ cli="lmfinance";
lmf.save_text("$WORK/dry.txt", "ran");
end _lmcode_
r = run_lmcode(p);
println(json_stringify(r));
LMS
"$LMSCRIPT" --dry-run "$WORK/dry.lms" >/dev/null 2>&1 || { cat "$WORK/dry.lms" >&2; fail "dry-run invocation"; }
[ ! -e "$WORK/dry.txt" ] || fail "--dry-run executed the program"
capture dry
"$HOST" --dry-run --lmcode "$WORK/dry.prog" >"$WORK/dry-host.out" || fail "host dry run"
[ ! -e "$WORK/dry.txt" ] || fail "standalone --dry-run executed the program"
run_lms "$WORK/dry.lms" >/dev/null && [ -e "$WORK/dry.txt" ] || fail "program did not run without --dry-run"
ok "--dry-run verifies without running (invocation metadata and standalone flag)"

# README gallery: every linked example and image exists, images are valid PNG/GIF within the size limits.
magic_ok() { # file
  case "$1" in
    *.png) [ "$(head -c 8 "$1" | od -An -tx1 | tr -d ' \n')" = "89504e470d0a1a0a" ];;
    *.gif) [ "$(head -c 4 "$1")" = "GIF8" ];;
    *) return 1;;
  esac
}
nimg=0
for img in $(grep -oE 'docs/images/[A-Za-z0-9_.-]+\.(png|gif)' README.md | sort -u); do
  [ -f "$img" ] || fail "README references missing image $img"
  magic_ok "$img" || fail "$img is not a valid PNG/GIF"
  size=$(wc -c <"$img" | tr -d ' ')
  case "$img" in *.gif) limit=3000000;; *) limit=300000;; esac
  [ "$size" -le "$limit" ] || fail "$img is $size bytes (limit $limit)"
  nimg=$((nimg+1))
done
[ "$nimg" -ge 6 ] || fail "README gallery shows only $nimg images"
for ex in $(grep -oE '\(examples/[A-Za-z0-9_/.-]*\.lms\)' README.md | tr -d '()' | sort -u); do
  [ -f "$ex" ] || fail "README links missing example $ex"
done
grep -qE 'lmctl( autorun)? https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/' README.md || fail "README captions lack lmctl run commands"
ok "README gallery: $nimg images valid and within size limits, every linked example exists, captions use lmctl autorun"

# Gallery examples: lmfinance computes, lmplot or lmdraw draws. They run only when a COMPATIBLE
# renderer is already on PATH (one that answers --lmcode-schema): a release that still speaks the
# retired protocol is not a usable renderer, and an auto-install would fetch exactly that. Any
# example whose renderer is not on PATH is skipped loudly; CI never fetches another host at @latest.
have_lmplot=false; have_lmdraw=false
command -v lmplot >/dev/null 2>&1 && lmplot --lmcode-schema --json >/dev/null 2>&1 && have_lmplot=true || true
command -v lmdraw >/dev/null 2>&1 && lmdraw --lmcode-schema --json >/dev/null 2>&1 && have_lmdraw=true || true
if $have_lmplot || $have_lmdraw; then
  ng=0
  for src in examples/gallery/*.lms; do
    name="$(basename "$src" .lms)"
    if grep -q 'cli="lmplot"' "$src" && ! $have_lmplot; then echo "note: $name skipped (no compatible lmplot on PATH)"; continue; fi
    if grep -q 'cli="lmdraw"' "$src" && ! $have_lmdraw; then echo "note: $name skipped (no compatible lmdraw on PATH)"; continue; fi
    if [ "$name" = option_decay ] && ! command -v ffmpeg >/dev/null 2>&1; then echo "note: $name skipped (no ffmpeg on PATH)"; continue; fi
    timeout 300 "$LMSCRIPT" "$src" >"$WORK/gallery-$name.log" 2>&1 || { cat "$WORK/gallery-$name.log" >&2; fail "gallery example $name"; }
    ng=$((ng+1))
  done
  for img in images/*.png images/*.gif; do
    [ -s "$img" ] || continue
    magic_ok "$img" || fail "$img is not a valid PNG/GIF"
  done
  ok "$ng gallery examples run and wrote valid images (docs/images are refreshed by examples/render-all.sh)"
else
  echo "SKIPPED: gallery examples (no compatible lmplot/lmdraw on PATH): docs/images were not regenerated"
  ok "gallery examples skipped loudly (no compatible lmplot/lmdraw on PATH)"
fi

# Memory budget: a runaway loop stops with a run error (exit 3, message on stderr and in the JSON
# frame) instead of exhausting the machine. Bumped curves are the biggest host objects a loop can
# make quickly (~1.3 KB each, kept in the arena). A small budget everywhere, the Monte Carlo
# sample pre-check, and on Linux the default 4096 MiB under a 6 GiB address-space limit (ulimit
# only here, never in the host).
cat >"$WORK/runaway.lms" <<LMS
p = _lmcode_ cli="lmfinance";
let c = market.flat_curve(0.03);
let i = 0;
while (i < 100000000) {
  let d = c.bumped(0.0001);
  i = i + 1;
}
end _lmcode_
r = run_lmcode(p);
println(json_stringify(r));
LMS
capture runaway
set +e; out="$(LMFINANCE_MAX_MEMORY_MB=64 "$HOST" --lmcode "$WORK/runaway.prog" 2>"$WORK/runaway.err")"; code=$?; set -e
[ "$code" = 3 ] || fail "runaway loop under LMFINANCE_MAX_MEMORY_MB=64: exit $code, expected 3 ($out)"
echo "$out" | grep -q "memory budget exceeded" || fail "runaway loop: JSON lacks the budget message: $out"
grep -q "LMFINANCE_MAX_MEMORY_MB" "$WORK/runaway.err" || fail "runaway loop: no budget message on stderr"
printf 'p = _lmcode_ cli="lmfinance";\nlet r = pricing.price(instrument.european_call(100.0, 1.0), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.monte_carlo(100000000, 7));\nend _lmcode_\nr = run_lmcode(p);\nprintln(json_stringify(r));\n' >"$WORK/bigmc.lms"
capture bigmc
set +e; out="$(LMFINANCE_MAX_MEMORY_MB=64 "$HOST" --lmcode "$WORK/bigmc.prog" 2>/dev/null)"; code=$?; set -e
[ "$code" = 3 ] && echo "$out" | grep -q "2289 MiB more requested" || fail "1e8 Monte Carlo paths under LMFINANCE_MAX_MEMORY_MB=64: exit $code ($out)"
set +e; LMFINANCE_MAX_MEMORY_MB=-5 "$HOST" --lmcode "$WORK/runaway.prog" >/dev/null 2>"$WORK/badenv.err"; code=$?; set -e
[ "$code" = 2 ] && grep -q "positive whole number" "$WORK/badenv.err" || fail "bad LMFINANCE_MAX_MEMORY_MB: exit $code"
if [ "$UNAME_S" = Linux ]; then
  set +e; out="$(ulimit -v 6291456; "$HOST" --lmcode "$WORK/runaway.prog" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] && echo "$out" | grep -q "limit 4096 MiB" || fail "runaway loop with the default budget under ulimit -v 6 GiB: exit $code ($out)"
  ok "memory budget: runaway loop stops at 64 MiB and at the default 4096 MiB (under ulimit -v 6 GiB), Monte Carlo pre-check"
else
  ok "memory budget: runaway loop and Monte Carlo pre-check stop at LMFINANCE_MAX_MEMORY_MB=64 (default-budget run is Linux-only)"
fi

set +e; "$HOST" --lmcode "$WORK/does-not-exist.prog" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 3 ] || fail "missing program exit code $code, expected 3"
set +e; "$HOST" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 2 ] || fail "usage exit code $code, expected 2"
ok "usage and load errors use exit codes 2 and 3"

echo "all $pass checks passed"
