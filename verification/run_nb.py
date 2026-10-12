import argparse
import hashlib
import json
import re
from pathlib import Path
import sys
import run

ROOT=run.ROOT


def check(log,summary,directed=True,inputs=None,words=4):
    lines=log.read_text().splitlines()
    match=re.fullmatch(r'CACHE NB PASS accepted=(\d+) completed=(\d+) cycles=(\d+) max_reads=(\d+)',lines[-1]) if lines else None
    if not match or any('FAIL' in line or 'Error' in line for line in lines):
        raise ValueError('missing terminal concurrent evidence')
    data=json.loads(summary.read_text())
    if data['status']!='PASS' or data['accepted']!=data['completed']+data['cancelled_by_reset'] or data['accepted']==0:
        raise ValueError('response conservation')
    if tuple(map(int,match.groups()))!=(data['accepted'],data['completed'],data['cycles'],data['max_outstanding_reads']):
        raise ValueError('summary/terminal mismatch')
    if data['read_beats']+data['aborted_read_beats']!=words*data['read_bursts'] or \
       data['write_beats']+data['aborted_write_beats']!=words*data['write_bursts']+data['orphan_write_beats']:
        raise ValueError('beat conservation')
    if inputs:
        for path,want in json.loads(inputs.read_text())['sha256'].items():
            if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=want:raise ValueError('stale source hashes')
    if directed:
        bins=['hit_under_miss','two_outstanding_read_bursts','bank1_refill_before_bank0',
              'same_bank_stall','bad_BID_retention','flush_drain_inflight','flush_error',
              'AW_stall','W_stall','AR_stall','response_stall','AW_before_W','W_before_AW']
        bins += [f'mask{x}' for x in range(16)]
        bins += ['destructive_dirty_reset','inflight_two_read_reset','inflight_write_reset',
                 'invalid_RID_containment','protocol_fault_reset_recovery','partial_write_reset',
                 'orphan_W_before_AW_reset','bank0_contested_RR','bank1_contested_RR']
        bins += [f'bank{bank}_Rerror_beat{beat}' for bank in range(2) for beat in range(words)]
        bins += [f'bank{bank}_RLAST_mode{mode}' for bank in range(2) for mode in (1,2)]
        bins += [f'bank{bank}_interleaved_Rerror_beat{beat}' for bank in range(2) for beat in range(words)]
        bins += [f'bank{bank}_interleaved_RLAST_mode{mode}' for bank in range(2) for mode in (1,2)]
        bins += [f'bank{bank}_request_WB_failure_{kind}' for bank in range(2) for kind in ('BID','BRESP','live_BID')]
        bins += ['failed_writeback_uncommitted_beats']
        if data['max_outstanding_reads']!=2 or any(data['coverage'].get(x,0)==0 for x in bins):
            raise ValueError('unwitnessed concurrent coverage')
    return data


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);p.add_argument('--seed',type=int,default=1)
    p.add_argument('--operations',type=int,default=1000);p.add_argument('--random-only',action='store_true')
    p.add_argument('--sets',type=int,default=4);p.add_argument('--words',type=int,default=4)
    p.add_argument('--rtl',type=Path,default=ROOT/'rtl/cache_nb.sv')
    p.add_argument('--core-rtl',type=Path,default=ROOT/'rtl/cache.sv')
    a=p.parse_args();out=(ROOT/a.out).resolve()
    if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    out.mkdir(parents=True,exist_ok=False)
    sources=[str(a.core_rtl.resolve().relative_to(ROOT)),str(a.rtl.resolve().relative_to(ROOT)),'verification/tb_nb.cpp','verification/run_nb.py']
    (out/'inputs.json').write_text(json.dumps({'sha256':{x:hashlib.sha256((ROOT/x).read_bytes()).hexdigest() for x in sources},'arguments':vars(a)|{'rtl':str(a.rtl),'core_rtl':str(a.core_rtl)}},indent=2))
    cmd=['verilator','--cc','--exe','--build','--assert','-DCACHE_ASSERT','-Wall','-Wno-PINCONNECTEMPTY',
         '--top-module','cache_nb',f'-GSETS={a.sets}',f'-GWORDS={a.words}',
         '-CFLAGS',f'-DTEST_SETS={a.sets} -DTEST_WORDS={a.words}',
         '--Mdir',str(out/'obj'),str(a.core_rtl.resolve()),str(a.rtl.resolve()),str(ROOT/'verification/tb_nb.cpp')]
    rc=run.execute(cmd,out/'compile.log')
    if rc:raise RuntimeError(f'compile exit {rc}, {out}/compile.log')
    rc=run.execute([str(out/'obj/Vcache_nb'),str(a.seed),str(a.operations),str(int(not a.random_only)),str(out/'summary.json')],out/'run.log')
    if rc:raise RuntimeError(f'run exit {rc}, {out}/run.log')
    data=check(out/'run.log',out/'summary.json',not a.random_only,out/'inputs.json',a.words)
    print(f"VERIFIED NB {out.name} requests={data['accepted']} max_outstanding_reads={data['max_outstanding_reads']}")


if __name__=='__main__':
    try:main()
    except (ValueError,RuntimeError,KeyError,OSError) as e:print(f'NB REGRESSION FAIL {e}',file=sys.stderr);sys.exit(1)
