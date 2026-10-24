# Linux dynamic glibc rollout (Rule 14)

v0.1.3 uses dynamic glibc >= 2.35 on Linux. CMake builds QuantLib as a static archive, uses Boost headers only, disables QuantLib OpenMP and restricts Linux library searches to .a. libstdc++ and libgcc are static; full -static is used only on Windows. macOS continues to use system libraries only.

Both Linux CI jobs use Ubuntu 22.04. test.sh runs eight ELF-checker regressions and validates the binary with scripts/check-linux-linkage.py: libc.so.6 must be dynamic, only glibc library/loader names are allowed, and readelf rejects GLIBC versions newer than 2.35. The ldd fallback reports that it checks names and missing libraries only when readelf is unavailable. No relink-object archive is generated or shipped. --version remains exactly lmfinance X.Y.Z for installer compatibility.

Local verification: all 18 test.sh checks pass with gallery networking enabled, including all 28 gallery examples. Three additional fresh-directory README runs pass using the candidate binary: option_greeks, barrier_options, var_tail. Linux x64 is 5,405,992 bytes, with NEEDED libm.so.6, libc.so.6, ld-linux-x86-64.so.2 and maximum GLIBC_2.34. The documented and tested release baseline remains glibc >= 2.35.
