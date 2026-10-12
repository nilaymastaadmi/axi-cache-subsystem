import hashlib, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
manifest=json.loads((ROOT/'EXPORT-MANIFEST.json').read_text())
for rel,want in manifest['sha256'].items():
    path=(ROOT/rel).resolve()
    if not path.is_relative_to(ROOT) or not path.is_file():raise ValueError(f'missing export file {rel}')
    if hashlib.sha256(path.read_bytes()).hexdigest()!=want:raise ValueError(f'export hash mismatch {rel}')
if not manifest['sha256']:raise ValueError('empty manifest')
print(f"EXPORT SOURCE CHECK PASS files={len(manifest['sha256'])}")
