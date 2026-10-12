#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="${OSS_CAD_BIN:-$HOME/tools/oss-cad-suite/bin}:$PATH"
out="$1"
case "$out" in results/*) ;; *) exit 1 ;; esac
mkdir -p results
test ! -e "$out-terminal.log"
bash scripts/release_check.sh "$out" 2>&1 | tee "$out-terminal.log"
