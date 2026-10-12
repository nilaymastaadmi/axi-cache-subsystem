import argparse, hashlib, json, re, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'verification'))
import run

def check(folder,variant,workload,latency):
    lines=(folder/'run.log').read_text().splitlines()
    match=re.fullmatch(r'COMPARE PASS requests=(\d+) cycles=(\d+)',lines[-1]) if lines else None
    if not match or any('FAIL' in x or 'Error' in x for x in lines):raise ValueError('missing clean terminal')
    data=json.loads((folder/'summary.json').read_text())
    if data['status']!='PASS' or data['accepted']!=4096 or data['completed']!=4096:raise ValueError('request conservation')
    if tuple(map(int,match.groups()))!=(data['completed'],data['cycles']):raise ValueError('terminal mismatch')
    if data['workload']!=workload or data['latency']!=latency or data['flush_cycles']<=0:raise ValueError('population mismatch')
    words=1 if variant=='bypass' else 4
    if data['read_beats']!=words*data['read_bursts'] or data['write_beats']!=words*data['write_bursts']:raise ValueError('beat counts')
    if variant!='bypass' and data['hits']+data['misses']!=4096:raise ValueError('event count')
    if variant!='bypass' and workload=='same_bank_streaming' and data['misses']==0:raise ValueError('unwitnessed same-bank misses')
    raw=(folder/'summary.json.memory').read_bytes()
    if len(raw)!=65536:raise ValueError('truncated final memory')
    return data|{'variant':variant,'final_memory_sha256':hashlib.sha256(raw).hexdigest(),
                'external_bytes':4*(data['read_beats']+data['write_beats']),
                'requests_per_cycle':4096/data['cycles'],'mean_response_latency':data['latency_sum']/4096}

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
    out=(ROOT/a.out).resolve()
    if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    out.mkdir(parents=True,exist_ok=False)
    sources=['rtl/cache.sv','rtl/cache_nb.sv','measurement/cache_compare.sv','measurement/tb_compare.cpp','measurement/run_compare.py','measurement/PROTOCOL.md']
    hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sources}
    (out/'inputs.json').write_text(json.dumps({'sha256':hashes},indent=2));rows=[];images={}
    for mode,variant in enumerate(('blocking','concurrent','bypass')):
        folder=out/variant;folder.mkdir()
        cmd=['verilator','--cc','--exe','--build','--assert','-DCACHE_ASSERT','-Wall','-Wno-PINCONNECTEMPTY',
             '--top-module','cache_compare',f'-GMODE={mode}','--Mdir',str(folder/'obj'),
             str(ROOT/'rtl/cache.sv'),str(ROOT/'rtl/cache_nb.sv'),str(ROOT/'measurement/cache_compare.sv'),str(ROOT/'measurement/tb_compare.cpp')]
        if run.execute(cmd,folder/'compile.log'):raise RuntimeError(f'compile {variant}')
        for workload in ('locality','streaming','conflict','same_bank','same_bank_streaming'):
            for latency in (2,10,40):
                dest=folder/f'{workload}-{latency}';dest.mkdir()
                rc=run.execute([str(folder/'obj/Vcache_compare'),workload,str(latency),str(dest/'summary.json')],dest/'run.log')
                if rc:raise RuntimeError(f'run {dest}')
                data=check(dest,variant,workload,latency);data['source']=str(dest.relative_to(out))
                if workload in images and images[workload]!=data['final_memory_sha256']:raise ValueError('variant final memory mismatch')
                images[workload]=data['final_memory_sha256'];rows.append(data)
                print(f"MEASURED {variant} {workload} latency={latency} cycles={data['cycles']} bytes={data['external_bytes']}",flush=True)
    for f,want in hashes.items():
        if hashlib.sha256((ROOT/f).read_bytes()).hexdigest()!=want:raise ValueError('stale measurement source')
    (out/'results.json').write_text(json.dumps({'status':'PASS','rows':rows,'scope':'simulated cycles, final flush included'},indent=2))
    print('COMPARISON PASS rows=45 requests_per_row=4096 memory_variants_equal=5')
if __name__=='__main__':main()
