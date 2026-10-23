# Libraries and licenses

| library | version | license | how |
| --- | --- | --- | --- |
| QuantLib | v1.44 (`ab4cb476`, pinned git commit) | modified BSD ("BSD-like", full text in THIRD_PARTY_NOTICES) | compiled from source into the binary |
| Boost | 1.89.0 headers | BSL-1.0 | release tarball pinned by sha256; only the `boost/` header tree is extracted; nothing linked |
| cpp-dsl-sdk | `dslvm.hpp`, pinned in `sdk.lock` | MIT | vendored single header |

No GPL/AGPL/LGPL code anywhere. `lmfinance --notices` prints THIRD_PARTY_NOTICES (embedded).

## deps.lock

Lines are `name repo-or-url pin`. A 40-hex pin is a git commit (QuantLib). A 64-hex pin is the sha256
of a release tarball (Boost: `boost-1.89.0-b2-nodocs.tar.gz`, 73 MB, from github.com/boostorg/boost
releases; the git superproject would need ~150 submodules). `scripts/sync-deps.sh` verifies the sha256
before extracting `boost-1.89.0/boost` into git-ignored `third_party/boost` (184 MB of headers, 10 s).
The `.pin` file inside records what is extracted; `--verify` compares it.

## Pruned QuantLib build

`CMakeLists.txt` adds QuantLib through its own CMake (it generates `ql/config.hpp`, `qldefines.hpp`)
with tests/examples/install off, then edits the `ql_library` source list: drops `ql/experimental/*`
(except `experimental/coupons`, which the core coupon pricers reference through `CmsSpreadCoupon`),
`ql/legacy` and `ql/models/marketmodels`. That is 717 of 985 translation units. Link succeeds
because nothing used refers to the dropped trees. Compiled `-O1` (QuantLib) and `-O2` (host code)
`-DNDEBUG`; QuantLib warnings are silenced (`-w`).

Two CMake workarounds: QuantLib exports its include dirs for `install()`, and CMake rejects a Boost
include path inside our source tree, so the boost path is filtered out of the exported interface
properties and added privately; policy CMP0167 (FindBoost removal) is set to OLD for QuantLib's
`find_package(Boost)`, which is satisfied by `Boost_INCLUDE_DIR`.
