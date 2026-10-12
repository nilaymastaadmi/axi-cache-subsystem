import argparse, hashlib, json, sys
from pathlib import Path
import run, run_nb
ROOT=run.ROOT
FAULTS={
 'R_buffer_data':('rbuf_data<=rdata;',"rbuf_data<=rdata ^ 32'b1;"),
 'R_buffer_ID':('rbuf_id<=rid;',"rbuf_id<=rid ^ 2'b1;"),
 'bank_address':('bank_araddr[ar_owner][30:OFFSET],ar_owner,','bank_araddr[ar_owner][30:OFFSET],!ar_owner,'),
 'write_unlock':('if(!write_busy&&|write_candidates&&!protocol_error)','if(|write_candidates&&!protocol_error)'),
 'flush_error':('if(|(bank_flush_done&bank_flush_error))flush_error<=1;','if(|(bank_flush_done&bank_flush_error))flush_error<=0;'),
 'request_identity':('.req_id(req_id),',".req_id(req_id ^ 16'b1),"),
 'failed_B_dirty_loss': ('end else begin\n                            rsp_error <= 1;',
        'end else begin\n                            dirty[victim_set][victim_way] <= 0;\n                            rsp_error <= 1;'),
}

def replay(binary,base,dest,seed,count,directed=True):
    dest.mkdir()
    meta=json.loads((base/'inputs.json').read_text());meta['arguments']['seed']=seed
    meta['arguments']['operations']=count;meta['arguments']['random_only']=not directed
    (dest/'inputs.json').write_text(json.dumps(meta,indent=2))
    rc=run.execute([str(binary),str(seed),str(count),str(int(directed)),str(dest/'summary.json')],dest/'run.log')
    if rc:raise RuntimeError(f'replay failed {dest}')
    return run_nb.check(dest/'run.log',dest/'summary.json',directed,dest/'inputs.json')

def controls(out,base):
    log=(base/'run.log').read_text();data=json.loads((base/'summary.json').read_text())
    variants={'empty_log':('',data),'missing_terminal':('build completed\n',data),
      'count_mismatch':(log,data|{'completed':data['completed']+1}),
      'beat_mismatch':(log,data|{'read_beats':data['read_beats']+1}),
      'missing_coverage':(log,data|{'coverage':{}}),
      'bad_concurrency':(log,data|{'max_outstanding_reads':1}),
      'truncated_JSON':(log,'{')}
    root=out/'controls';root.mkdir();results={}
    run_nb.check(base/'run.log',base/'summary.json',True,base/'inputs.json')
    for name,(text,value) in variants.items():
        folder=root/name;folder.mkdir();(folder/'run.log').write_text(text)
        (folder/'summary.json').write_text(value if isinstance(value,str) else json.dumps(value))
        try:run_nb.check(folder/'run.log',folder/'summary.json')
        except (ValueError,KeyError) as e:results[name]={'status':'REJECTED','reason':str(e)}
        else:raise RuntimeError(f'control accepted {name}')
    meta=json.loads((base/'inputs.json').read_text());meta['sha256']['rtl/cache_nb.sv']='0'*64
    (root/'stale.json').write_text(json.dumps(meta))
    try:run_nb.check(base/'run.log',base/'summary.json',True,root/'stale.json')
    except ValueError as e:results['stale_source']={'status':'REJECTED','reason':str(e)}
    else:raise RuntimeError('stale source accepted')
    (root/'results.json').write_text(json.dumps(results,indent=2));return results

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
    out=(ROOT/a.out).resolve()
    if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    out.mkdir(parents=True,exist_ok=False);base=out/'baseline'
    rc=run.execute([sys.executable,'-B',str(ROOT/'verification/run_nb.py'),'--out',str(base)],out/'baseline-wrapper.log')
    if rc:raise RuntimeError(f'baseline failed {base}')
    binary=base/'obj/Vcache_nb';dev=[{'seed':1,'summary':json.loads((base/'summary.json').read_text())}]
    for seed in range(2,9):
        dev.append({'seed':seed,'summary':replay(binary,base,out/f'dev-{seed}',seed,1000)})
    faults=[]
    for name,(anchor,replacement) in FAULTS.items():
        child=name=='failed_B_dirty_loss'
        original=(ROOT/('rtl/cache.sv' if child else 'rtl/cache_nb.sv')).read_text()
        if original.count(anchor)!=1:raise ValueError(f'nonunique anchor {name}')
        folder=out/'mutants'/name;folder.mkdir(parents=True);rtl=folder/('cache.sv' if child else 'cache_nb.sv')
        rtl.write_text(original.replace(anchor,replacement))
        rc=run.execute([sys.executable,'-B',str(ROOT/'verification/run_nb.py'),'--out',str(folder/'run'),
                        '--core-rtl' if child else '--rtl',str(rtl),'--operations','0'],folder/'wrapper.log')
        log=folder/'run/run.log'
        if not log.is_file():raise RuntimeError(f'{name} not executed compiled fault')
        witness=[x for x in log.read_text().splitlines() if 'CACHE NB FAIL' in x or '%Error' in x]
        if rc==0 or not witness:raise RuntimeError(f'{name} survived or lacked witness')
        faults.append({'name':name,'status':'DETECTED','witness':witness[0],
                       'sha256':hashlib.sha256(rtl.read_bytes()).hexdigest()})
        print(f'NB FAULT DETECTED {name}: {witness[0]}',flush=True)
    rejected=controls(out,base)
    (out/'development.json').write_text(json.dumps({'status':'PASS','development':dev,'faults':faults,'controls':rejected},indent=2))
    held=[]
    for seed in range(201,221):
        held.append({'seed':seed,'summary':replay(binary,base,out/f'holdout-{seed}',seed,2000,False)})
    configs=[]
    for sets,words in ((4,2),(8,8)):
        folder=out/f'geometry-{sets}x{words}'
        rc=run.execute([sys.executable,'-B',str(ROOT/'verification/run_nb.py'),'--out',str(folder),
                        '--sets',str(sets),'--words',str(words)],out/f'geometry-{sets}x{words}.log')
        if rc:raise RuntimeError(f'configuration failed {folder}')
        configs.append({'sets':sets,'words':words,'summary':json.loads((folder/'summary.json').read_text())})
    (out/'closure.json').write_text(json.dumps({'status':'PASS','held_out':held,'configurations':configs},indent=2))
    print('NB CLOSURE PASS development_seeds=8 compiled_faults=7 evidence_controls=8 held_out_seeds=20 configurations=2')

if __name__=='__main__':main()
