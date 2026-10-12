import hashlib, json, sys
from pathlib import Path
import run

ROOT=run.ROOT
out=(ROOT/sys.argv[1]).resolve()
if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
out.mkdir(parents=True,exist_ok=False)
original=(ROOT/'rtl/cache_nb.sv').read_text()
anchor='rid[1]||!read_outstanding[rid[0]]'
if original.count(anchor)!=1:raise ValueError('nonlive ID mutation anchor')
mutant=out/'cache_nb.sv';mutant.write_text(original.replace(anchor,'rid[1]'))
sources=['rtl/cache.sv','rtl/cache_nb.sv','verification/tb_nb_id_fault.sv','verification/id_fault_control.py']
(out/'inputs.json').write_text(json.dumps({'sha256':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sources},
    'mutant_sha256':hashlib.sha256(mutant.read_bytes()).hexdigest()},indent=2))
rc=run.execute(['iverilog','-g2012','-s','tb_nb_id_fault','-Ptb_nb_id_fault.BAD_ID=1',
    '-o',str(out/'fault.vvp'),str(ROOT/'rtl/cache.sv'),str(mutant),str(ROOT/'verification/tb_nb_id_fault.sv')],out/'compile.log')
if rc:raise RuntimeError('nonlive ID control did not compile')
rc=run.execute(['vvp',str(out/'fault.vvp')],out/'run.log')
if rc==0 or 'ID fault containment' not in (out/'run.log').read_text():raise RuntimeError('nonlive ID mutant lacked containment witness')
(out/'results.json').write_text(json.dumps({'status':'PASS','fault':'removed non-live RID detection',
    'fault_status':'DETECTED','witness':'ID fault containment','exit':rc},indent=2))
print('NB ID CONTROL PASS compiled_nonlive_RID_fault_detected=1')
