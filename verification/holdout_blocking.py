import argparse, json, sys
import run
ROOT=run.ROOT
def main():
    p=argparse.ArgumentParser();p.add_argument('--suite',required=True);p.add_argument('--out',required=True);a=p.parse_args()
    suite=(ROOT/a.suite).resolve();out=(ROOT/a.out).resolve()
    if not suite.is_relative_to(ROOT/'results') or not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
    if json.loads((suite/'suite.json').read_text())['status']!='PASS':raise ValueError('suite gate')
    base=suite/'baseline';run.check(base/'run.log',base/'summary.json',4,base/'inputs.json',True)
    out.mkdir(parents=True,exist_ok=False);rows=[]
    for seed in range(101,121):
        folder=out/f'seed-{seed}';folder.mkdir();inputs=json.loads((base/'inputs.json').read_text())
        inputs['arguments']['seed']=seed;inputs['arguments']['operations']=2000;inputs['arguments']['random_only']=True
        (folder/'inputs.json').write_text(json.dumps(inputs,indent=2))
        if run.execute([str(base/'obj/Vcache'),str(seed),'2000','0',str(folder/'summary.json')],folder/'run.log'):raise RuntimeError(f'holdout {seed}')
        rows.append({'seed':seed,'summary':run.check(folder/'run.log',folder/'summary.json',4,folder/'inputs.json')})
    for sets,words in ((2,2),(8,8)):
        folder=out/f'geometry-{sets}x{words}'
        cmd=[sys.executable,'-B',str(ROOT/'verification/run.py'),'--out',str(folder),'--sets',str(sets),'--words',str(words),'--operations','1000']
        if run.execute(cmd,out/f'geometry-{sets}x{words}.log'):raise RuntimeError('geometry gate')
    (out/'closure.json').write_text(json.dumps({'status':'PASS','holdout':rows,'configurations':2},indent=2))
    print('BLOCKING HOLDOUT PASS seeds=20 requests=40000 configurations=2')
if __name__=='__main__':main()
