"""Fresh-directory builds; a zero exit without terminal evidence never passes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import re

ROOT = Path(__file__).resolve().parents[1]


def execute(command, log, timeout=180):
    with log.open('w') as stream:
        try:
            result = subprocess.run(command, cwd=ROOT, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            stream.write('\nWRAPPER TIMEOUT\n')
            raise RuntimeError(f'timeout, retained {log}')
    return result.returncode


def check(log, summary, words, inputs=None, directed=False):
    lines = log.read_text().splitlines()
    terminal = re.fullmatch(r'CACHE PASS accepted=(\d+) completed=(\d+) cycles=(\d+)',lines[-1]) if lines else None
    if not terminal or any('FAIL' in line or 'Error' in line for line in lines):
        raise ValueError('missing terminal CACHE PASS')
    data = json.loads(summary.read_text())
    if data['status'] != 'PASS' or data['accepted'] != data['completed'] + data['cancelled_by_reset'] or data['accepted'] <= 0:
        raise ValueError('invalid completion counts')
    if tuple(map(int,terminal.groups())) != (data['accepted'],data['completed'],data['cycles']):
        raise ValueError('terminal/JSON mismatch')
    if data['read_beats'] + data['aborted_read_beats'] != data['read_bursts'] * words:
        raise ValueError('read burst/beat mismatch')
    if data['write_beats'] + data['aborted_write_beats'] != data['write_bursts'] * words + data['orphan_write_beats']:
        raise ValueError('write burst/beat mismatch')
    if inputs:
        for path,expected in json.loads(inputs.read_text())['sha256'].items():
            if hashlib.sha256((ROOT/path).read_bytes()).hexdigest() != expected:
                raise ValueError('stale source hashes')
    if directed:
        required = ['AW_stall','W_stall','AR_stall','response_stall','flush_response_stall',
                    'R_gap','B_gap','AW_before_W','W_before_AW','round_robin_contested_grant',
                    'cold_and_hit','both_ways_LRU_dirty_eviction','simultaneous_request_routing',
                    'unaligned_rejection','dirty_error_retention','bad_RID','bad_RLAST','bad_BID',
                    'flush_error','flush_success','destructive_dirty_reset','inflight_refill_reset',
                    'inflight_write_reset','4KiB_adjacent_lines','failed_writeback_uncommitted_beats']
        required += [f'mask{x}' for x in range(16)]
        required += [f'allocate_mask{x}' for x in range(1,16)]
        required += [f'refill_error_beat{x}' for x in range(words)]
        absent = [bin for bin in required if data['coverage'].get(bin,0) <= 0]
        if absent:
            raise ValueError(f'unwitnessed directed bins {absent}')
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True)
    parser.add_argument('--sets', type=int, default=4)
    parser.add_argument('--words', type=int, default=4)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--operations', type=int, default=0)
    parser.add_argument('--random-only', action='store_true')
    parser.add_argument('--rtl', type=Path, default=ROOT/'rtl/cache.sv')
    args = parser.parse_args()
    out = (ROOT/args.out).resolve()
    if not out.is_relative_to(ROOT/'results'):
        raise ValueError('output must be within results')
    out.mkdir(parents=True, exist_ok=False)
    sources = [args.rtl.resolve(), ROOT/'verification/tb.cpp', Path(__file__).resolve()]
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (out/'inputs.json').write_text(json.dumps({'sha256':hashes,'arguments':vars(args)|{'rtl':str(args.rtl)}},indent=2))
    command = ['verilator','--cc','--exe','--build','--assert','-DCACHE_ASSERT','-Wall',
               '--top-module','cache',f'-GSETS={args.sets}',f'-GWORDS={args.words}',
               '-CFLAGS',f'-DTEST_SETS={args.sets} -DTEST_WORDS={args.words}',
               '--Mdir',str(out/'obj'),str(args.rtl.resolve()),str(ROOT/'verification/tb.cpp')]
    rc = execute(command,out/'compile.log')
    if rc:
        raise RuntimeError(f'compile exit {rc}, {out}/compile.log')
    command = [str(out/'obj/Vcache'),str(args.seed),str(args.operations),
               str(int(not args.random_only)),str(out/'summary.json')]
    rc = execute(command,out/'run.log')
    if rc:
        raise RuntimeError(f'run exit {rc}, {out}/run.log')
    data = check(out/'run.log',out/'summary.json',args.words,out/'inputs.json',not args.random_only)
    print(f"VERIFIED {out.name}: accepted={data['accepted']} completed={data['completed']} cycles={data['cycles']}")


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError,ValueError,KeyError,OSError) as error:
        print(f'REGRESSION FAIL: {error}',file=sys.stderr)
        sys.exit(1)
