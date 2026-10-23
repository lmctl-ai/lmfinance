#!/usr/bin/env bash
# Fetch every dependency in deps.lock into git-ignored third_party/NAME at its pin.
# 40-hex pin: git commit. 64-hex pin: sha256 of a release tarball; only its boost/ header tree is extracted.
# --verify only checks that each checkout is at its pin.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }
while read -r name repo pin; do
  case "$name" in ''|\#*) continue;; esac
  dir="$HERE/third_party/$name"
  if [ "${#pin}" = 64 ]; then
    if [ "${1:-}" = "--verify" ]; then
      [ "$(cat "$dir/.pin" 2>/dev/null)" = "$pin" ] || { echo "third_party/$name missing or not at deps.lock pin: run scripts/sync-deps.sh" >&2; exit 1; }
      continue
    fi
    [ "$(cat "$dir/.pin" 2>/dev/null)" = "$pin" ] && { echo "third_party/$name already at $pin"; continue; }
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/lmfinance-dep-XXXXXX")"
    for i in 1 2 3; do curl -fsSL -o "$tmp/a.tgz" "$repo" && break; echo "download attempt $i failed" >&2; sleep 5; done
    [ "$(sha "$tmp/a.tgz")" = "$pin" ] || { echo "$name: sha256 of $repo differs from deps.lock" >&2; exit 1; }
    rm -rf "$dir"; mkdir -p "$dir"
    top="$(tar -tzf "$tmp/a.tgz" | head -1)"; top="${top%%/*}"
    tar -xzf "$tmp/a.tgz" -C "$dir" --strip-components=1 "$top/boost"
    echo "$pin" > "$dir/.pin"; rm -rf "$tmp"
    echo "synced third_party/$name at $pin"
    continue
  fi
  if [ "${1:-}" = "--verify" ]; then
    [ -d "$dir/.git" ] || { echo "third_party/$name missing: run scripts/sync-deps.sh" >&2; exit 1; }
    have="$(git -C "$dir" rev-parse HEAD)"
    [ "$have" = "$pin" ] || { echo "third_party/$name at $have, deps.lock pins $pin" >&2; exit 1; }
    continue
  fi
  if [ ! -d "$dir/.git" ]; then mkdir -p "$dir"; git -C "$dir" init -q; git -C "$dir" remote add origin "$repo"; fi
  git -C "$dir" fetch -q --depth 1 origin "$pin"
  git -C "$dir" checkout -q -f FETCH_HEAD
  echo "synced third_party/$name at $pin"
done < "$HERE/deps.lock"
[ "${1:-}" = "--verify" ] && echo "third_party matches deps.lock"
exit 0
