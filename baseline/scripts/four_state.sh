#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
out="$1"
case "$out" in results/*) ;; *) exit 1 ;; esac
mkdir "$out"
iverilog -g2012 -DCACHE_ASSERT -s tb_four_state -o "$out/test.vvp" rtl/cache.sv verification/tb_four_state.sv > "$out/compile.log" 2>&1
vvp "$out/test.vvp" > "$out/run.log" 2>&1
grep -qx 'FOUR STATE PASS requests=3 dirty_flush_beats=4' "$out/run.log"
echo 'FOUR STATE PASS (Icarus)'
