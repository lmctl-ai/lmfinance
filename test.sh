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

# lmscript resolves cli="lmfinance" on PATH first, so programs bind to the binary under test.
export PATH="$(cd "$BUILD_DIR" 2>/dev/null && pwd || echo "$HERE/$BUILD_DIR"):$PATH"
LMSCRIPT="$(scripts/select-compiler.sh)"
scripts/sync-sdk.sh --verify >/dev/null || fail "SDK header pin mismatch"
ok "SDK header matches sdk.lock"
scripts/sync-deps.sh --verify >/dev/null || fail "third_party pin mismatch"
ok "third_party matches deps.lock"

GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
# shellcheck disable=SC2086
cmake -S . -B "$BUILD_DIR" $GEN -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" >"$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log" >&2; fail "build"; }
ok "build"

want_version="lmfinance $(sed -n 's/^project(lmfinance VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ "$("$HOST" --version)" = "$want_version" ] || fail "--version is not exactly '$want_version'"
ok "--version prints exactly '$want_version'"
python3 scripts/gen-schema.py >/dev/null && git diff --quiet -- schema/lmfinance.dsl.json 2>/dev/null || [ ! -e .git ] || fail "schema/lmfinance.dsl.json is stale: run scripts/gen-schema.py"
ok "schema matches the registrations in cpp/main.cpp"
"$HOST" --dsl-schema --json >"$WORK/schema.json" || fail "embedded schema print"
cmp -s "$WORK/schema.json" schema/lmfinance.dsl.json || fail "embedded schema differs from schema/lmfinance.dsl.json"
ok "--dsl-schema --json prints byte-identical embedded schema bytes"
diff -u <("$LMSCRIPT" --dsl-signatures schema/lmfinance.dsl.json) <("$HOST" --dsl-signatures) || fail "host/compiler signatures differ"
ok "host registry matches compiler signatures"
"$HOST" --notices | grep -q "QuantLib" || fail "--notices missing third-party text"
"$HOST" --notices >"$WORK/notices.txt" || fail "--notices"
for n in "Makoto Matsumoto" "University of Chicago" "Boost C++ Libraries"; do
  grep -q "$n" "$WORK/notices.txt" || fail "--notices missing the $n notice"
done
ok "--notices prints the third-party license texts"

case "$UNAME_S" in
  Linux) if ldd "$HOST" >/dev/null 2>&1; then fail "executable is dynamically linked"; fi; ok "executable is fully static (no dynamic loader)";;
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

# Programs: compile every test and example, run them (they assert their own numbers).
mkdir -p build/examples
n=0
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$LMSCRIPT" "$src" "build/$name.dslb" >"$WORK/$name.log" 2>&1 || { cat "$WORK/$name.log" >&2; fail "$name compile"; }
  n=$((n+1))
done
ok "$n programs compile as bytecode v5"
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --dsl "build/$name.dslb" >"$WORK/$name.out" 2>"$WORK/$name.err" || { cat "$WORK/$name.out" "$WORK/$name.err" >&2; fail "$name run"; }
  grep -q '"ok": true' "$WORK/$name.out" || { cat "$WORK/$name.out" >&2; fail "$name did not report ok"; }
done
ok "$n programs run and pass their own assertions"

# Determinism: the CSV files the examples write are identical on rerun.
for f in build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/before.txt"
for src in examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --dsl "build/$name.dslb" >/dev/null 2>&1 || fail "$name rerun"
done
for f in build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/after.txt"
cmp -s "$WORK/before.txt" "$WORK/after.txt" || fail "result files differ on rerun"
ok "all $(wc -l <"$WORK/after.txt" | tr -d ' ') result files are byte-identical on rerun"

# Error paths: each snippet must fail the run with exit 3 and a message naming the problem.
printf '1 2 3\n' >"$WORK/pnl.csv"
expect_error() { # name, snippet, expected message fragment
  cat >"$WORK/$1.lms" <<LMS
p = _dsl_ cli="lmfinance";
$2
end _dsl_
savedsl(p, "$WORK/$1.dslb");
LMS
  "$LMSCRIPT" "$WORK/$1.lms" >/dev/null 2>"$WORK/$1.cerr" || { cat "$WORK/$1.cerr" >&2; fail "$1 compile"; }
  set +e; out="$("$HOST" --dsl "$WORK/$1.dslb" 2>/dev/null)"; code=$?; set -e
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
ok "errors stop the program with exit 3 and a clear message (28 cases)"

# --dry-run loads and verifies bytecode but runs nothing (no output files).
cat >"$WORK/dry.lms" <<LMS
p = _dsl_ cli="lmfinance";
lmf.save_text("$WORK/dry.txt", "ran");
end _dsl_
savedsl(p, "$WORK/dry.dslb");
LMS
"$LMSCRIPT" "$WORK/dry.lms" >/dev/null 2>&1 || fail "dry-run program compile"
"$HOST" --dry-run --dsl "$WORK/dry.dslb" | grep -q '"ok": true' || fail "dry run did not report ok"
[ ! -e "$WORK/dry.txt" ] || fail "--dry-run executed the program"
"$HOST" --dsl "$WORK/dry.dslb" >/dev/null && [ -e "$WORK/dry.txt" ] || fail "program did not run without --dry-run"
ok "--dry-run verifies without running"

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
grep -q 'lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmfinance/main/examples/' README.md || fail "README captions lack lmctl autorun commands"
ok "README gallery: $nimg images valid and within size limits, every linked example exists, captions use lmctl autorun"

# Gallery examples: lmfinance computes, lmplot draws (needs lmplot on PATH, or network for install=; the GIF needs ffmpeg).
if command -v lmplot >/dev/null 2>&1 || [ "${LMFINANCE_TEST_NETWORK:-}" = 1 ]; then
  ng=0
  for src in examples/gallery/*.lms; do
    name="$(basename "$src" .lms)"
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
  ok "gallery examples skipped (no lmplot on PATH, LMFINANCE_TEST_NETWORK unset): docs/images not regenerated"
fi

# Memory budget: a runaway loop stops with a run error (exit 3, message on stderr and in the JSON
# line) instead of exhausting the machine. Bumped curves are the biggest host objects a loop can
# make quickly (~1.3 KB each, kept in the arena). A small budget everywhere, the Monte Carlo
# sample pre-check, and on Linux the default 4096 MiB under a 6 GiB address-space limit (ulimit
# only here, never in the host).
cat >"$WORK/runaway.lms" <<LMS
p = _dsl_ cli="lmfinance";
let c = market.flat_curve(0.03);
let i = 0;
while (i < 100000000) {
  let d = c.bumped(0.0001);
  i = i + 1;
}
end _dsl_
savedsl(p, "$WORK/runaway.dslb");
LMS
"$LMSCRIPT" "$WORK/runaway.lms" >/dev/null || fail "runaway.lms compile"
set +e; out="$(LMFINANCE_MAX_MEMORY_MB=64 "$HOST" --dsl "$WORK/runaway.dslb" 2>"$WORK/runaway.err")"; code=$?; set -e
[ "$code" = 3 ] || fail "runaway loop under LMFINANCE_MAX_MEMORY_MB=64: exit $code, expected 3 ($out)"
echo "$out" | grep -q "memory budget exceeded" || fail "runaway loop: JSON lacks the budget message: $out"
grep -q "LMFINANCE_MAX_MEMORY_MB" "$WORK/runaway.err" || fail "runaway loop: no budget message on stderr"
printf 'p = _dsl_ cli="lmfinance";\nlet r = pricing.price(instrument.european_call(100.0, 1.0), model.black_scholes(100.0, 0.05, 0.0, 0.2), pricing.monte_carlo(100000000, 7));\nend _dsl_\nsavedsl(p, "%s");\n' \
  "$WORK/bigmc.dslb" >"$WORK/bigmc.lms"
"$LMSCRIPT" "$WORK/bigmc.lms" >/dev/null || fail "bigmc.lms compile"
set +e; out="$(LMFINANCE_MAX_MEMORY_MB=64 "$HOST" --dsl "$WORK/bigmc.dslb" 2>/dev/null)"; code=$?; set -e
[ "$code" = 3 ] && echo "$out" | grep -q "2289 MiB more requested" || fail "1e8 Monte Carlo paths under LMFINANCE_MAX_MEMORY_MB=64: exit $code ($out)"
set +e; LMFINANCE_MAX_MEMORY_MB=-5 "$HOST" --dsl "$WORK/runaway.dslb" >/dev/null 2>"$WORK/badenv.err"; code=$?; set -e
[ "$code" = 2 ] && grep -q "positive whole number" "$WORK/badenv.err" || fail "bad LMFINANCE_MAX_MEMORY_MB: exit $code"
if [ "$UNAME_S" = Linux ]; then
  set +e; out="$(ulimit -v 6291456; "$HOST" --dsl "$WORK/runaway.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] && echo "$out" | grep -q "limit 4096 MiB" || fail "runaway loop with the default budget under ulimit -v 6 GiB: exit $code ($out)"
  ok "memory budget: runaway loop stops at 64 MiB and at the default 4096 MiB (under ulimit -v 6 GiB), Monte Carlo pre-check"
else
  ok "memory budget: runaway loop and Monte Carlo pre-check stop at LMFINANCE_MAX_MEMORY_MB=64 (default-budget run is Linux-only)"
fi

set +e; "$HOST" --dsl "$WORK/does-not-exist.dslb" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 3 ] || fail "missing bytecode exit code $code, expected 3"
set +e; "$HOST" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 2 ] || fail "usage exit code $code, expected 2"
ok "usage and load errors use exit codes 2 and 3"

echo "all $pass checks passed"
