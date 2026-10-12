import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from run import ROOT,execute


def config(source,extra=False,mode='bmc',depth=24,solver='bitwuzla'):
    return f'''[options]
mode {mode}
depth {depth}
timeout 600
[engines]
smtbmc {solver}
[script]
read -formal -sv -D CACHE_ASSERT cache.sv
chparam -set SETS 2 -set WORDS 2 cache
prep -top cache
memory_map
[files]
cache.sv {source}
'''


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True)
    p.add_argument('--cover-only',action='store_true')
    p.add_argument('--solver',choices=('z3','bitwuzla'),default='bitwuzla')
    p.add_argument('--resume-extra',action='store_true');a=p.parse_args()
    out=(ROOT/a.out).resolve()
    if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    if not a.resume_extra:out.mkdir(parents=True,exist_ok=False)
    original=(ROOT/'rtl/cache.sv').read_text()
    extra=(ROOT/'formal/extra.svh').read_text()
    if original.count('endmodule')!=1:raise ValueError('module anchor')
    augmented=original.replace('endmodule','`ifdef FORMAL\n'+extra+'`endif\nendmodule')
    augmented_path=out/'augmented.sv'
    inputs={'rtl_sha256':hashlib.sha256((ROOT/'rtl/cache.sv').read_bytes()).hexdigest(),
            'extra_sha256':hashlib.sha256((ROOT/'formal/extra.svh').read_bytes()).hexdigest()}
    if a.cover_only:
        if a.resume_extra:raise ValueError('cover cannot resume extra')
        augmented_path.write_text(augmented)
        (out/'inputs.json').write_text(json.dumps(inputs,indent=2))
        (out/'cover.sby').write_text(config(augmented_path,mode='cover',depth=32,solver=a.solver))
        rc=execute(['sby','-d',str(out/'cover'),str(out/'cover.sby')],out/'cover.log',650)
        status=(out/'cover/status').read_text().split()[0]
        if rc or status!='PASS':raise RuntimeError(f'dirty lifecycle cover {status}, retained {out}')
        traces=list((out/'cover/engine_0').glob('trace*.vcd'))
        # One trace may reach multiple cover statements on the same edge.
        reached=(out/'cover.log').read_text().count('Reached cover statement')
        if reached!=8 or not traces:raise RuntimeError('expected six core plus two dirty lifecycle covers with traces')
        (out/'results.json').write_text(json.dumps({'status':'PASS','covers':8,'traces':len(traces),'depth':32,'solver':a.solver},indent=2))
        print('FORMAL DIRTY COVER PASS covers=8 depth=32');return
    if a.resume_extra:
        if json.loads((out/'inputs.json').read_text())!=inputs or augmented_path.read_text()!=augmented:
            raise ValueError('resume source mismatch')
        if (out/'fault').exists() or (out/'fault.sv').exists():raise ValueError('fault already attempted')
        if 'DONE (PASS' not in (out/'extra.log').read_text():raise ValueError('missing completed formal log')
        rc=0
    else:
        augmented_path.write_text(augmented)
        (out/'inputs.json').write_text(json.dumps(inputs,indent=2))
        (out/'extra.sby').write_text(config(augmented_path,solver=a.solver))
        rc=execute(['sby','-d',str(out/'extra'),str(out/'extra.sby')],out/'extra.log',650)
    status=(out/'extra/status').read_text().split()[0]
    if rc or status!='PASS':raise RuntimeError(f'augmented formal {status}, retained {out}')
    anchor='assign wdata = data[victim_set][victim_way][beat[WW-1:0]];'
    if original.count(anchor)!=1:raise ValueError('mutation anchor')
    mutated=original.replace(anchor,"assign wdata = data[victim_set][victim_way][beat[WW-1:0]] ^ (wready ? 32'b0 : 32'b1);")
    fault=out/'fault.sv';fault.write_text(mutated)
    (out/'fault.sby').write_text(config(fault,depth=24,solver=a.solver))
    rc=execute(['sby','-d',str(out/'fault'),str(out/'fault.sby')],out/'fault.log',650)
    status=(out/'fault/status').read_text().split()[0]
    if rc==0 or status!='FAIL' or not list((out/'fault/engine_0').glob('trace*.vcd')):
        raise RuntimeError('formal fault not falsified with executed trace')
    (out/'results.json').write_text(json.dumps({'status':'PASS','extra_bounded_depth':24,
                                              'fault_status':status,'assumptions':'initial reset only; no progress/fairness assumptions'},indent=2))
    print('FORMAL CONTROLS PASS response/dirty safety depth=24; W-instability falsified with trace')


if __name__=='__main__':main()
