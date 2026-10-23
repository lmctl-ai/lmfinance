#!/usr/bin/env bash
# Refresh the README previews after a test run with the combo examples: ./test.sh && scripts/update-renders.sh
set -eu
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p examples/renders
cp build/examples/yield_curve.png build/examples/option_greeks.png examples/renders/
