# Linux dynamic glibc rollout (Rule 14)

v0.1.3 uses dynamic glibc >= 2.35 on Linux. CMake builds QuantLib as a static archive, uses Boost headers only, disables QuantLib OpenMP and restricts Linux library searches to .a. libstdc++ and libgcc are static; full -static is used only on Windows. macOS continues to use system libraries only.

Both Linux CI jobs use Ubuntu 22.04. test.sh runs eight ELF-checker regressions and validates the binary with scripts/check-linux-linkage.py: libc.so.6 must be dynamic, only glibc library/loader names are allowed, and readelf rejects GLIBC versions newer than 2.35. The ldd fallback reports that it checks names and missing libraries only when readelf is unavailable. No relink-object archive is generated or shipped. --version remains exactly lmfinance X.Y.Z for installer compatibility.

Local verification: all 18 test.sh checks pass with gallery networking enabled, including all 28 gallery examples. Three additional fresh-directory README runs pass using the candidate binary: option_greeks, barrier_options, var_tail. Linux x64 is 5,405,992 bytes, with NEEDED libm.so.6, libc.so.6, ld-linux-x86-64.so.2 and maximum GLIBC_2.34. The documented and tested release baseline remains glibc >= 2.35.

## Published v0.1.3 verification

All five tag-platform jobs and the release job passed in [CI](https://github.com/lmctl-ai/lmfinance/actions/runs/40058046303). All five binaries, notices, licenses and SHA256SUMS are attached, without a relink-object archive. Empty-directory autorun with an isolated fresh cache installed v0.1.3 and wrote images/option_greeks.png (51,365 bytes). Both Linux assets passed the dependency/ABI and checksum checks. Linux x64 is 5,405,992 bytes; SHA-256: 9606778c9ce09cf1a6060d0c4d4712f0bef91a436bb89b33170c6bb7c6ed2c1a.
