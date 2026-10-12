#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
out="$1"
case "$out" in results/*) ;; *) exit 1 ;; esac
python3 -B verification/export_check.py
bash scripts/suite.sh --out "$out-blocking"
bash scripts/holdout_blocking.sh --suite "$out-blocking" --out "$out-blocking-holdout"
bash scripts/suite_nb.sh --out "$out-concurrent"
bash scripts/four_state_nb.sh "$out-four-state"
bash scripts/id_faults.sh "$out-id-faults"
bash scripts/synth_nb.sh "$out-synthesis"
bash scripts/compare.sh --out "$out-measurement"
bash scripts/compare_controls.sh --out "$out-measurement-controls" --baseline "$out-measurement"
bash scripts/integration.sh --out "$out-integration"
bash scripts/integration_control.sh --out "$out-integration-control"
echo 'CLEAN EXPORT REPLAY PASS (independent review/formal assessed separately)'
