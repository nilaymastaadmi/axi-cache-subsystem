#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../formal"
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
out="$1"
case "$out" in results/*) ;; *) echo 'results output required'; exit 1 ;; esac
mkdir "../$out"
sby -d "../$out/bmc" safety.sby bmc > "../$out/bmc.log" 2>&1
sby -d "../$out/cover" safety.sby cover > "../$out/cover.log" 2>&1
read -r bmc_status _ < "../$out/bmc/status"
read -r cover_status _ < "../$out/cover/status"
test "$bmc_status" = PASS
test "$cover_status" = PASS
echo 'FORMAL PASS bounded_depth=32 reduced_geometry=2x2'
