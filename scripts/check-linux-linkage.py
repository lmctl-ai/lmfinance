#!/usr/bin/env python3
"""Check the Linux dynamic dependencies and glibc baseline of an executable."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


TIMEOUT_SECONDS = 30
MAX_GLIBC = (2, 35, 0)
ALLOWED_NEEDED = {
    "libc.so.6",
    "libm.so.6",
    "libpthread.so.0",
    "libdl.so.2",
    "librt.so.1",
}
GLIBC_VERSION_RE = re.compile(r"GLIBC_(\d+)(?:\.(\d+))?(?:\.(\d+))?")
NEEDED_RE = re.compile(r"\(NEEDED\).*?\[([^]]+)\]")


class LinkageError(Exception):
    """A linkage policy violation or an inability to inspect the executable."""


def _run(command: list[str]) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
            env={**os.environ, "LC_ALL": "C"},
        )
    except FileNotFoundError as exc:  # pragma: no cover - guarded by which()
        raise LinkageError(f"required command unavailable: {command[0]}") from exc
    except subprocess.TimeoutExpired as exc:
        raise LinkageError(f"{command[0]} timed out after {TIMEOUT_SECONDS}s") from exc
    except OSError as exc:
        raise LinkageError(f"could not run {command[0]}: {exc}") from exc


def _version_tuple(match: re.Match[str]) -> tuple[int, int, int]:
    return tuple(int(match.group(index) or 0) for index in range(1, 4))  # type: ignore[return-value]


def _check_glibc_versions(version_info: str) -> None:
    versions = [_version_tuple(match) for match in GLIBC_VERSION_RE.finditer(version_info)]
    too_new = sorted({version for version in versions if version > MAX_GLIBC})
    if too_new:
        rendered = ", ".join(
            f"{version[0]}.{version[1]}" if version[2] == 0 else ".".join(str(part) for part in version)
            for version in too_new
        )
        raise LinkageError(f"requires glibc newer than 2.35: GLIBC_{rendered}")


def _check_needed(needed: list[str], source: str) -> None:
    if "libc.so.6" not in needed:
        raise LinkageError(f"{source} executable does not dynamically require libc.so.6 (static or invalid executable)")
    unexpected = sorted(
        dependency
        for dependency in set(needed)
        if dependency not in ALLOWED_NEEDED and not (dependency.startswith("ld-linux") and "/" not in dependency)
    )
    if unexpected:
        raise LinkageError(f"disallowed dynamic dependencies: {', '.join(unexpected)}")


def _check_readelf(host: str, readelf: str) -> None:
    dynamic = _run([readelf, "-d", host])
    if dynamic.returncode != 0:
        details = (dynamic.stderr or dynamic.stdout).strip().splitlines()
        suffix = f": {details[-1]}" if details else ""
        raise LinkageError(f"readelf could not inspect {host}{suffix}")
    needed = NEEDED_RE.findall(dynamic.stdout)
    _check_needed(needed, "dynamic")

    versions = _run([readelf, "--version-info", host])
    if versions.returncode != 0:
        details = (versions.stderr or versions.stdout).strip().splitlines()
        suffix = f": {details[-1]}" if details else ""
        raise LinkageError(f"readelf could not inspect glibc version requirements{suffix}")
    _check_glibc_versions(versions.stdout)


def _ldd_dependencies(output: str) -> list[str]:
    dependencies: list[str] = []
    for line in output.splitlines():
        line = line.strip()
        if not line or line.startswith("statically linked") or line.startswith("not a dynamic executable"):
            continue
        # linux-vdso is supplied by the kernel and is not a DT_NEEDED entry.
        if line.startswith("linux-vdso"):
            continue
        if "=>" in line:
            name = line.split("=>", 1)[0].strip()
        else:
            name = line.split(None, 1)[0]
        dependencies.append(Path(name).name)
    return dependencies


def _check_ldd(host: str, ldd: str) -> None:
    result = _run([ldd, host])
    output = "\n".join(part for part in (result.stdout, result.stderr) if part)
    if "statically linked" in output or "not a dynamic executable" in output:
        raise LinkageError(f"{host} is static; a dynamically linked glibc executable is required")
    if result.returncode != 0 or "not found" in output:
        raise LinkageError(f"ldd could not resolve all dependencies of {host}: {output.strip()}")
    _check_needed(_ldd_dependencies(output), "ldd")
    print("note: glibc symbol-version audit requires readelf; ldd verified dependency names only")


def check(host: str) -> None:
    """Validate *host*, raising LinkageError when the policy is violated."""
    readelf = shutil.which("readelf")
    if readelf:
        _check_readelf(host, readelf)
        return
    ldd = shutil.which("ldd")
    if not ldd:
        raise LinkageError("neither readelf nor ldd is available")
    _check_ldd(host, ldd)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"usage: {argv[0]} HOST", file=sys.stderr)
        return 2
    try:
        check(argv[1])
    except LinkageError as exc:
        print(f"linux linkage check failed: {exc}", file=sys.stderr)
        return 1
    print(f"linux linkage check passed: {argv[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
