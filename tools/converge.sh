#!/bin/sh
# Re-lift -> build -> run until the runtime stops finding new dispatch targets.
# usage: tools/converge.sh [game dir] [rounds]   (env: PCRECOMP_HOME, SSO_KEYS, SSO_KEYSEQ, SSO_SECONDS)
cd "$(dirname "$0")/.."
DIR=${1:-..}; N=${2:-8}
export PCRECOMP_HOME=${PCRECOMP_HOME:-$PWD/.pcrecomp}
cnt() { sort -u work/sso_misses.txt 2>/dev/null | wc -l; }
for i in $(seq 1 "$N"); do
  before=$(cnt)
  python3 tools/lift_full.py | grep -E "feedback|^lifted"
  cmake --build build 2>&1 | grep -E "error|undefined"
  SSO_SECONDS=${SSO_SECONDS:-10} ./build/sso work/SSO_unpacked.exe "$DIR" >/dev/null 2>&1
  after=$(cnt)
  echo "round $i: misses $before -> $after"
  [ "$before" = "$after" ] && break
done
