import argparse, hashlib, json, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'verification'))
import run
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
    out=(ROOT/a.out).resolve()
    if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    out.mkdir(parents=True,exist_ok=False)
    source=(ROOT/'integration/tb_core.cpp').read_text();anchor='d.cpu_dmem_rdata=c.response_data[port];'
    if source.count(anchor)!=1:raise ValueError('CPU response anchor')
    # C++ relative include resolves against the copied source location.
    changed=source.replace(anchor,'d.cpu_dmem_rdata=0;').replace('#include "../measurement/tb_compare.cpp"',
            '#include "'+str(ROOT/'measurement/tb_compare.cpp')+'"')
    fault=out/'tb_fault.cpp';fault.write_text(changed)
    assert all(b in (9,10) or 32<=b<=126 for b in fault.read_bytes())
    (out/'inputs.json').write_text(json.dumps({'fault_sha256':hashlib.sha256(fault.read_bytes()).hexdigest(),
       'original_sha256':hashlib.sha256((ROOT/'integration/tb_core.cpp').read_bytes()).hexdigest()},indent=2))
    cmd=['verilator','--cc','--exe','--build','--assert','-DCACHE_ASSERT','-Wall','-Wno-PINCONNECTEMPTY',
       '--top-module','rv32_cache_top','-GMODE=1','--Mdir',str(out/'obj'),str(ROOT/'integration/core_waivers.vlt'),
       str(ROOT/'rtl/cache.sv'),str(ROOT/'rtl/cache_nb.sv'),str(ROOT/'measurement/cache_compare.sv'),
       str(ROOT/'integration/rv32_cache_top.sv'),str(ROOT/'integration/vendor/rv32i_core.v'),str(fault)]
    if run.execute(cmd,out/'compile.log'):raise RuntimeError('integration fault not compiled')
    rc=run.execute([str(out/'obj/Vrv32_cache_top'),str(out/'summary.json')],out/'run.log')
    witness=[x for x in (out/'run.log').read_text().splitlines() if 'CORE CACHE FAIL LW initial' in x]
    if not rc or not witness:raise RuntimeError('CPU response-disconnect fault not detected')
    (out/'results.json').write_text(json.dumps({'status':'PASS','compiled_fault':witness[0]},indent=2))
    print('INTEGRATION CONTROL PASS CPU response-disconnect fault detected')
if __name__=='__main__':main()
