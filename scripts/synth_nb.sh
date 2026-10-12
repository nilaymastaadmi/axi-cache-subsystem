#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
out="$1"
case "$out" in results/*) ;; *) exit 1 ;; esac
mkdir "$out"
yosys -V > "$out/version.txt"
sha256sum rtl/cache.sv rtl/cache_nb.sv > "$out/source.sha256"
for geometry in '4 2' '4 4' '8 8'; do
  read -r sets words <<< "$geometry"
  yosys -Q -T -p "read_verilog -sv rtl/cache.sv; chparam -set SETS $((sets/2)) -set WORDS $words cache; hierarchy -check -top cache; read_verilog -sv rtl/cache_nb.sv; chparam -set SETS $sets -set WORDS $words cache_nb; hierarchy -check -top cache_nb; rename -top mapped_cache_nb; proc; write_json $out/hierarchy-s${sets}w${words}.json" > "$out/elaborate-s${sets}w${words}.log" 2>&1
  yosys -Q -T -p "read_json $out/hierarchy-s${sets}w${words}.json; synth -top mapped_cache_nb -flatten; check -assert; stat; write_json $out/s${sets}w${words}.json" > "$out/s${sets}w${words}.log" 2>&1
  if grep -E '(Latch inferred|ERROR:|Warning:.*(undriven|multiple conflicting))' "$out/s${sets}w${words}.log"; then exit 1; fi
done
python3 -B verification/audit_synth_nb.py "$out"
