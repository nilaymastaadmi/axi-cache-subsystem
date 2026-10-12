import argparse, hashlib, json, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'verification'))
import run
import run_compare
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);p.add_argument('--baseline',required=True);a=p.parse_args()
    base=(ROOT/a.baseline).resolve();out=(ROOT/a.out).resolve()
    if not base.is_relative_to(ROOT/'results') or not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    out.mkdir(parents=True,exist_ok=False)
    inputs=json.loads((base/'inputs.json').read_text())
    for f,want in inputs['sha256'].items():
        if hashlib.sha256((ROOT/f).read_bytes()).hexdigest()!=want:raise ValueError('baseline stale source')
    original=(ROOT/'measurement/cache_compare.sv').read_text();anchor='assign wdata=payload;'
    if original.count(anchor)!=1:raise ValueError('fault anchor')
    rtl=out/'cache_compare.sv';rtl.write_text(original.replace(anchor,"assign wdata=payload ^ 32'b1;"))
    cmd=['verilator','--cc','--exe','--build','--assert','-DCACHE_ASSERT','-Wall','-Wno-PINCONNECTEMPTY',
         '--top-module','cache_compare','-GMODE=2','--Mdir',str(out/'obj'),str(ROOT/'rtl/cache.sv'),
         str(ROOT/'rtl/cache_nb.sv'),str(rtl),str(ROOT/'measurement/tb_compare.cpp')]
    if run.execute(cmd,out/'compile.log'):raise RuntimeError('fault not compiled')
    rc=run.execute([str(out/'obj/Vcache_compare'),'locality','2',str(out/'summary.json')],out/'run.log')
    witness=[x for x in (out/'run.log').read_text().splitlines() if 'COMPARE FAIL dirty write byte' in x]
    if not rc or not witness:raise RuntimeError('benchmark byte fault not detected')
    good=base/'bypass/locality-2';data=run_compare.check(good,'bypass','locality',2);records={}
    raw=(good/'summary.json').read_text();log=(good/'run.log').read_text();image=(good/'summary.json.memory').read_bytes()
    variants={'empty_log':('',raw,image),'truncated_json':(log,'{',image),
      'count_mismatch':(log,json.dumps(data|{'completed':0}),image),
      'beat_mismatch':(log,json.dumps(data|{'read_beats':0}),image),
      'truncated_memory':(log,raw,image[:10])}
    for name,(text,value,img) in variants.items():
        folder=out/name;folder.mkdir();(folder/'run.log').write_text(text)
        (folder/'summary.json').write_text(value);(folder/'summary.json.memory').write_bytes(img)
        try:run_compare.check(folder,'bypass','locality',2)
        except (ValueError,KeyError) as e:records[name]={'status':'REJECTED','reason':str(e)}
        else:raise RuntimeError(f'control accepted {name}')
    (out/'results.json').write_text(json.dumps({'status':'PASS','compiled_fault':witness[0],'evidence_controls':records},indent=2))
    print('COMPARISON CONTROLS PASS compiled_byte_fault=1 evidence_controls=5')
if __name__=='__main__':main()
