#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
out="$1"
case "$out" in results/*) ;; *) exit 1 ;; esac
mkdir "$out"
sha256sum rtl/cache.sv rtl/cache_nb.sv verification/tb_nb_id_fault.sv > "$out/source.sha256"
for id in 1 2; do
  iverilog -g2012 -s tb_nb_id_fault -Ptb_nb_id_fault.BAD_ID="$id" -o "$out/id$id.vvp" rtl/cache.sv rtl/cache_nb.sv verification/tb_nb_id_fault.sv > "$out/id$id-compile.log" 2>&1
  vvp "$out/id$id.vvp" > "$out/id$id-run.log" 2>&1
  grep -q "^NB ID FAULT PASS bad_id=$id accepted=1 cancelled_by_reset=1$" "$out/id$id-run.log"
done
iverilog -g2012 -s tb_nb_id_fault -Ptb_nb_id_fault.BAD_ID=0 -Ptb_nb_id_fault.EXTRA_BEAT=1 -o "$out/excess.vvp" rtl/cache.sv rtl/cache_nb.sv verification/tb_nb_id_fault.sv > "$out/excess-compile.log" 2>&1
vvp "$out/excess.vvp" > "$out/excess-run.log" 2>&1
grep -q '^NB EXCESS R PASS bad_id=0 accepted=1 completed=1 cancelled_by_reset=0$' "$out/excess-run.log"
python3 -B verification/id_fault_control.py "$out/mutant"
echo 'NB ID CONTAINMENT PASS unused_ID=2 nonlive_ID=1 coordinated_reset=2'
echo 'NB EXCESS R CONTAINMENT PASS completed_burst_ID=0 coordinated_reset=1'
