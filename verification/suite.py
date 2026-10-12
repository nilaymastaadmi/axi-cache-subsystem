"""Serial registered development runs and real compiled fault witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import run

ROOT = run.ROOT
FAULTS = {
    'dirty_omission': ('dirty[set_index][hit_way] <= 1;', 'dirty[set_index][hit_way] <= 0;'),
    'tag_install': ('<= tag_value;', "<= tag_value ^ {{(TAGW-1){1'b0}}, 1'b1};"),
    'byte_mask': ('if (mask[lane])', 'if (!mask[lane])'),
    'refill_before_B': ('state <= WB_RESP;', 'state <= READ_ADDR;'),
    'W_instability': ('assign wdata = data[victim_set][victim_way][beat[WW-1:0]];',
                      "assign wdata = data[victim_set][victim_way][beat[WW-1:0]] ^ (wready ? 32'b0 : 32'b1);"),
    'response_identity': ('rsp_id <= req_id[req_ready[1]*8 +: 8];',
                          "rsp_id <= req_id[req_ready[1]*8 +: 8] ^ 8'b1;"),
    'failed_B_dirty_loss': ('end else begin\n                            rsp_error <= 1;',
        'end else begin\n                            dirty[victim_set][victim_way] <= 0;\n                            rsp_error <= 1;'),
}


def controls(out, baseline):
    source = baseline/'summary.json'
    log = baseline/'run.log'
    evidence = json.loads(source.read_text())
    control_dir = out/'evidence-controls'
    control_dir.mkdir()
    results = {}
    variants = {
        'empty_log': ('', source.read_text()),
        'missing_terminal': ('build completed\n', source.read_text()),
        'truncated_JSON': (log.read_text(), source.read_text()[:30]),
        'count_mismatch': (log.read_text(), json.dumps(evidence | {'completed':evidence['completed']+1})),
        'beat_mismatch': (log.read_text(), json.dumps(evidence | {'write_beats':evidence['write_beats']+1})),
        'missing_coverage': (log.read_text(), json.dumps(evidence | {'coverage':{}})),
    }
    run.check(log,source,4,baseline/'inputs.json',True)
    for name,(log_text,json_text) in variants.items():
        folder=control_dir/name
        folder.mkdir()
        (folder/'run.log').write_text(log_text)
        (folder/'summary.json').write_text(json_text)
        try:
            run.check(folder/'run.log',folder/'summary.json',4,None,True)
        except (ValueError,KeyError) as error:
            results[name]={'status':'REJECTED','reason':str(error)}
        else:
            raise RuntimeError(f'control accepted {name}')
    stale = json.loads((baseline/'inputs.json').read_text())
    first=next(iter(stale['sha256']))
    stale['sha256'][first]='0'*64
    (control_dir/'stale.json').write_text(json.dumps(stale))
    try:
        run.check(log,source,4,control_dir/'stale.json',True)
    except ValueError as error:
        results['stale_source']={'status':'REJECTED','reason':str(error)}
    else:
        raise RuntimeError('stale source accepted')
    (control_dir/'results.json').write_text(json.dumps(results,indent=2))
    return results


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',required=True)
    args=parser.parse_args()
    out=(ROOT/args.out).resolve()
    if not out.is_relative_to(ROOT/'results'):
        raise ValueError('results only')
    out.mkdir(parents=True,exist_ok=False)
    cmd=[sys.executable,'-B',str(ROOT/'verification/run.py'),'--out',str(out/'baseline'),
         '--operations','1000','--seed','1']
    subprocess.run(cmd,cwd=ROOT,check=True)
    baseline=out/'baseline'
    records=[]
    for seed in range(2,9):
        folder=out/f'dev-seed-{seed}'
        folder.mkdir()
        inputs=json.loads((baseline/'inputs.json').read_text())
        inputs['arguments']['seed']=seed
        (folder/'inputs.json').write_text(json.dumps(inputs,indent=2))
        code=run.execute([str(baseline/'obj/Vcache'),str(seed),'1000','1',str(folder/'summary.json')],folder/'run.log')
        if code:
            raise RuntimeError(f'seed {seed} fails, {folder}/run.log')
        data=run.check(folder/'run.log',folder/'summary.json',4,folder/'inputs.json',True)
        records.append({'seed':seed,'summary':data})
        print(f"DEV PASS seed={seed} accepted={data['accepted']}",flush=True)
    source=(ROOT/'rtl/cache.sv').read_text()
    mutants=[]
    for name,(anchor,replacement) in FAULTS.items():
        if source.count(anchor)!=1:
            raise ValueError(f'{name} anchor is not unique')
        folder=out/'mutants'/name
        folder.mkdir(parents=True)
        rtl=folder/'cache.sv'
        rtl.write_text(source.replace(anchor,replacement))
        cmd=[sys.executable,'-B',str(ROOT/'verification/run.py'),'--out',str(folder/'run'),
             '--rtl',str(rtl)]
        rc=run.execute(cmd,folder/'wrapper.log')
        compile_log=folder/'run/compile.log'
        simulation=folder/'run/run.log'
        if not compile_log.exists() or not simulation.exists():
            raise RuntimeError(f'{name}: not an executed compiled fault; {folder}/wrapper.log')
        lines=simulation.read_text().splitlines()
        witnesses=[line for line in lines if 'CACHE FAIL' in line or '%Error' in line]
        if rc==0 or not witnesses:
            raise RuntimeError(f'{name}: fault survived or harness failed without witness')
        mutants.append({'name':name,'status':'DETECTED','witness':witnesses[0],
                        'source_sha256':hashlib.sha256(rtl.read_bytes()).hexdigest()})
        print(f'FAULT DETECTED {name}: {witnesses[0]}',flush=True)
    rejected=controls(out,baseline)
    (out/'suite.json').write_text(json.dumps({'status':'PASS','development':records,
                                          'compiled_faults':mutants,'evidence_controls':rejected},indent=2))
    print('SUITE PASS development_seeds=8 compiled_faults=7 evidence_controls=7',flush=True)


if __name__=='__main__':
    main()
