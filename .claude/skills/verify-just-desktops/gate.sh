#!/bin/sh
# The CI gate (.github/workflows/ci.yml) plus a warning count: the build has no -Werror.
# usage: .claude/skills/verify-just-desktops/gate.sh   (from anywhere; logs go to $TMPDIR/jd-gate)
set -u
cd "$(dirname "$0")/../../.."
out="${TMPDIR:-/tmp}/jd-gate"
mkdir -p "$out"
rm -f bin/just-desktops
mise run build > "$out/build.log" 2>&1; b=$?
mise run test > "$out/test.log" 2>&1; t=$?
mise run test-san > "$out/san.log" 2>&1; s=$?
ver=$(./bin/just-desktops --version 2>&1); v=$?
mise run app > "$out/app.log" 2>&1; a=$?
w=$(cat "$out/build.log" "$out/app.log" | grep -c 'warning:')
echo "build=$b test=$t san=$s version=$v app=$a warnings=$w $ver"
[ "$b$t$s$v$a$w" = "000000" ]
