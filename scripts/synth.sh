#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
out="$1"
case "$out" in results/*) ;; *) echo 'results output required'; exit 1 ;; esac
mkdir "$out"
for geometry in '2 2' '4 4' '8 8'; do
  read -r sets words <<< "$geometry"
  yosys -Q -T -p "read_verilog -sv rtl/cache.sv; chparam -set SETS $sets -set WORDS $words cache; hierarchy -check -top cache; synth -top cache; check -assert; stat; write_json $out/s${sets}w${words}.json" > "$out/s${sets}w${words}.log" 2>&1
  if grep -E '(Latch inferred|ERROR:|Warning:.*(undriven|multiple conflicting))' "$out/s${sets}w${words}.log"; then exit 1; fi
done
sha256sum rtl/cache.sv > "$out/source.sha256"
echo 'SYNTH PASS geometries=3 (generic mapped cells, no physical area/timing claim)'
