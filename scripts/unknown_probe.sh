#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
mkdir results/unknown-probe
iverilog -g2012 -s unknown_probe -o results/unknown-probe/test.vvp verification/unknown_probe.sv
vvp results/unknown-probe/test.vvp | tee results/unknown-probe/run.log
