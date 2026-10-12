#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
out="$1"
case "$out" in results/*) ;; *) exit 1 ;; esac
mkdir "$out"
sha256sum rtl/cache.sv rtl/cache_nb.sv verification/tb_nb_four_state.sv > "$out/source.sha256"
iverilog -g2012 -s tb_nb_four_state -o "$out/valid.vvp" rtl/cache.sv rtl/cache_nb.sv verification/tb_nb_four_state.sv > "$out/compile.log" 2>&1
vvp "$out/valid.vvp" > "$out/run.log" 2>&1
grep -q '^NB FOUR STATE PASS requests=3 dirty_flush_beats=4$' "$out/run.log"
python3 - "$out" <<'PY'
from pathlib import Path
import sys
source=Path('verification/tb_nb_four_state.sv').read_text()
anchor="rdata=32'h78563412+32'(read_count);"
assert source.count(anchor)==1
fault=Path(sys.argv[1])/'unknown.sv'
fault.write_text(source.replace(anchor,"rdata=32'hxxxx3412+32'(read_count);"))
assert all(b in (9,10) or 32<=b<=126 for b in fault.read_bytes())
PY
iverilog -g2012 -s tb_nb_four_state -o "$out/fault.vvp" rtl/cache.sv rtl/cache_nb.sv "$out/unknown.sv" > "$out/fault-compile.log" 2>&1
if vvp "$out/fault.vvp" > "$out/fault-run.log" 2>&1; then echo 'unknown control survived'; exit 1; fi
grep -q 'unknown valid response' "$out/fault-run.log"
echo 'NB FOUR STATE AND X CONTROL PASS requests=3 dirty_flush_beats=4'
