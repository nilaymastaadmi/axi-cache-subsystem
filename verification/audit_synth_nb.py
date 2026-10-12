import hashlib, json, re, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out=(ROOT/sys.argv[1]).resolve()
if not out.is_relative_to(ROOT/'results'):raise ValueError('results only')
for line in (out/'source.sha256').read_text().splitlines():
    want,rel=line.split()
    if hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()!=want:raise ValueError('stale source')
rows=[]
for sets,words in ((4,2),(4,4),(8,8)):
    name=f's{sets}w{words}';log=(out/f'{name}.log').read_text()
    if 'Found and reported 0 problems' not in log or re.search(r'(ERROR:|Latch inferred|Warning:.*(?:undriven|multiple conflicting))',log):
        raise ValueError(f'unclean synthesis {name}')
    hierarchy=json.loads((out/f'hierarchy-{name}.json').read_text())['modules']
    banks=[x for x in hierarchy['mapped_cache_nb']['cells'].values() if x['type'] in hierarchy]
    if len(banks)!=2:raise ValueError('missing actual bank cores')
    cells=json.loads((out/f'{name}.json').read_text())['modules']['mapped_cache_nb']['cells']
    if not cells or any('LATCH' in c['type'].upper() for c in cells.values()):raise ValueError('empty or latch storage')
    rows.append({'sets':sets,'words':words,'generic_cells':len(cells),'source':f'{name}.json'})
(out/'summary.json').write_text(json.dumps({'status':'PASS','geometries':rows,'scope':'generic mapped logic only'},indent=2))
print('NB SYNTH PASS geometries=3 cells='+','.join(str(r['generic_cells']) for r in rows))
