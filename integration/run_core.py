import argparse, hashlib, json, re, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'verification'))
import run
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
    out=(ROOT/a.out).resolve()
    if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    out.mkdir(parents=True,exist_ok=False)
    sources=['rtl/cache.sv','rtl/cache_nb.sv','measurement/cache_compare.sv','measurement/tb_compare.cpp',
             'integration/rv32_cache_top.sv','integration/tb_core.cpp','integration/vendor/rv32i_core.v','integration/PROVENANCE.json','integration/core_waivers.vlt']
    hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sources}
    (out/'inputs.json').write_text(json.dumps({'sha256':hashes},indent=2));rows=[];trace=None;memory=None
    for mode,variant in enumerate(('blocking','concurrent','bypass')):
        folder=out/variant;folder.mkdir()
        cmd=['verilator','--cc','--exe','--build','--assert','-DCACHE_ASSERT','-Wall','-Wno-PINCONNECTEMPTY',
             '--top-module','rv32_cache_top',f'-GMODE={mode}','--Mdir',str(folder/'obj'),
             str(ROOT/'integration/core_waivers.vlt'),str(ROOT/'rtl/cache.sv'),str(ROOT/'rtl/cache_nb.sv'),str(ROOT/'measurement/cache_compare.sv'),
             str(ROOT/'integration/rv32_cache_top.sv'),str(ROOT/'integration/vendor/rv32i_core.v'),str(ROOT/'integration/tb_core.cpp')]
        if run.execute(cmd,folder/'compile.log'):raise RuntimeError(f'compile {variant}')
        if run.execute([str(folder/'obj/Vrv32_cache_top'),str(folder/'summary.json')],folder/'run.log'):raise RuntimeError(f'run {variant}')
        lines=(folder/'run.log').read_text().splitlines();m=re.fullmatch(r'CORE CACHE PASS retired=(\d+) requests=(\d+) cycles=(\d+)',lines[-1]) if lines else None
        data=json.loads((folder/'summary.json').read_text())
        if not m or any('FAIL' in x or 'Error' in x for x in lines):raise ValueError('missing clean terminal')
        if data['status']!='PASS' or tuple(map(int,m.groups()))!=(396,134,data['cycles']) or data['iterations']!=64:raise ValueError('integration counts')
        raw=(folder/'summary.json.trace').read_bytes();img=(folder/'summary.json.memory').read_bytes()
        if len(raw.splitlines())!=396 or len(img)!=65536:raise ValueError('truncated trace/memory')
        if (trace is not None and trace!=raw) or (memory is not None and memory!=img):raise ValueError('variant trace/memory mismatch')
        trace=raw;memory=img;rows.append(data|{'variant':variant,'trace_sha256':hashlib.sha256(raw).hexdigest(),
           'memory_sha256':hashlib.sha256(img).hexdigest(),'source':variant+'/summary.json'})
        print(f"INTEGRATED {variant} retired=396 requests=134 cycles={data['cycles']}",flush=True)
    for f,want in hashes.items():
        if hashlib.sha256((ROOT/f).read_bytes()).hexdigest()!=want:raise ValueError('stale source')
    (out/'results.json').write_text(json.dumps({'status':'PASS','variants':rows,'scope':'data-cache integration; uncached instruction ROM, manually clock-enabled core'},indent=2))
    print('INTEGRATION PASS variants=3 retired_per_variant=396 memory_requests_per_variant=134 traces_and_memory_equal')
if __name__=='__main__':main()
