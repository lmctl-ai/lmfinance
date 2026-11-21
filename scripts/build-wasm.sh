#!/usr/bin/env bash
# Build lmfinance (QuantLib) to WebAssembly with Emscripten. Run scripts/sync-deps.sh first.
#   EMSDK  Emscripten SDK root (default ~/emsdk)   BUILD  output directory (default build-wasm)   JOBS (default 3)
# Output: $BUILD/lmfinance.js and lmfinance.wasm (module factory createLmfinanceModule, one callMain per run).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMSDK="${EMSDK:-$HOME/emsdk}"; BUILD="${BUILD:-$HERE/build-wasm}"; JOBS="${JOBS:-3}"
EM="$EMSDK/upstream/emscripten"
mkdir -p "$BUILD"; cd "$BUILD"
"$EM/emcmake" cmake "$HERE" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-fwasm-exceptions" \
  -DCMAKE_EXE_LINKER_FLAGS="-fwasm-exceptions -sMODULARIZE=1 -sEXPORT_NAME=createLmfinanceModule -sENVIRONMENT=worker,node -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -sEXPORTED_RUNTIME_METHODS=callMain,FS,ENV -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=536870912 -sSTACK_SIZE=4194304"
cmake --build . -j"$JOBS" --target lmfinance
ls -l lmfinance.js lmfinance.wasm
