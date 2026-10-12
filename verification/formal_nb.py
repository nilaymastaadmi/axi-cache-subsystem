import argparse, hashlib, json
import run
ROOT=run.ROOT
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True)
    p.add_argument('--fault',action='store_true');p.add_argument('--solver',choices=('z3','bitwuzla','yices'),default='bitwuzla')
    p.add_argument('--count-fault',action='store_true')
    p.add_argument('--task',choices=('all','bmc','cover'),default='all');p.add_argument('--depth',type=int,default=32)
    a=p.parse_args()
    if a.fault and a.count_fault:raise ValueError('one fault per run')
    fault=a.fault or a.count_fault
    if not 1<=a.depth<=64:raise ValueError('bounded depth 1..64')
    out=(ROOT/a.out).resolve()
    if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    out.mkdir(parents=True,exist_ok=False)
    source=(ROOT/'rtl/cache_nb.sv').read_text();extra=(ROOT/'formal/nb_extra.svh').read_text()
    if source.count('endmodule')!=1:raise ValueError('module anchor')
    augmented=source.replace('endmodule','`ifdef FORMAL\n'+extra+'`endif\nendmodule')
    if a.fault:
        anchor='.req_id(req_id),'
        if augmented.count(anchor)!=1:raise ValueError('fault anchor')
        augmented=augmented.replace(anchor,".req_id(req_id ^ 16'b1),")
    if a.count_fault:
        anchor="==($clog2(WORDS)+1)'(WORDS-1)"
        if augmented.count(anchor)!=1:raise ValueError('count fault anchor')
        augmented=augmented.replace(anchor,"==($clog2(WORDS)+1)'(WORDS)")
    (out/'cache_nb.sv').write_text(augmented)
    child=(ROOT/'rtl/cache.sv').read_text()
    if child.count('endmodule')!=1:raise ValueError('child module anchor')
    child=child.replace('endmodule','`ifdef FORMAL\n'+(ROOT/'formal/extra.svh').read_text()+'`endif\nendmodule')
    (out/'cache.sv').write_text(child)
    (out/'inputs.json').write_text(json.dumps({'sha256':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest()
       for f in ('rtl/cache.sv','rtl/cache_nb.sv','formal/extra.svh','formal/nb_extra.svh','verification/formal_nb.py')}},indent=2))
    config=f'''[tasks]
bmc
cover
[options]
bmc: mode bmc
cover: mode cover
depth {a.depth}
timeout 600
[engines]
smtbmc {a.solver}
[script]
read -formal -sv -D CACHE_ASSERT cache.sv
chparam -set SETS 2 -set WORDS 2 cache
hierarchy -check -top cache
read -formal -sv -D CACHE_ASSERT cache_nb.sv
chparam -set SETS 4 -set WORDS 2 cache_nb
hierarchy -check -top cache_nb
rename -top formal_cache_nb
prep -top formal_cache_nb
memory_map
[files]
cache.sv {out/'cache.sv'}
cache_nb.sv {out/'cache_nb.sv'}
'''
    (out/'safety.sby').write_text(config)
    records={}
    tasks=('bmc',) if fault else (('bmc','cover') if a.task=='all' else (a.task,))
    for task in tasks:
        rc=run.execute(['sby','-d',str(out/task),str(out/'safety.sby'),task],out/f'{task}.log',650)
        status=(out/task/'status').read_text().split()[0];records[task]={'exit':rc,'status':status}
        (out/'results.json').write_text(json.dumps({'depth':a.depth,'geometry':'4x2','fault':fault,
            'fault_kind':'read_count' if a.count_fault else ('identity' if a.fault else None),'solver':a.solver,'tasks':records},indent=2))
        if fault:
            if rc==0 or status!='FAIL' or not list((out/task/'engine_0').glob('trace*.vcd')):
                raise RuntimeError('formal fault not falsified with trace')
            kind='read_count' if a.count_fault else 'ID'
            print(f'NB FORMAL CONTROL PASS {kind} fault falsified with trace');return
        if rc or status!='PASS':raise RuntimeError(f'{task} {status}; retained {out}')
    print(f'NB FORMAL PASS task={a.task} bounded_depth={a.depth} geometry=4x2 solver={a.solver}')
if __name__=='__main__':main()
