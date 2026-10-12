#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="$1"
case "$out" in results/*) ;; *) exit 1 ;; esac
for task in bmc cover; do
  read -r status _ < "$out/$task/status"
  test "$status" = PASS
  grep -q 'DONE (PASS, rc=0)' "$out/$task.log"
done
covers=$(grep -c 'Reached cover statement' "$out/cover.log")
test "$covers" -eq 6
echo "FORMAL AUDIT PASS bounded_depth=32 covers=$covers reduced_geometry=2x2"
