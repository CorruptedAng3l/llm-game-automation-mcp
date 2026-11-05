"""Extract only reviewed production bodies for the pure-memory cursor model."""
from pathlib import Path
import argparse
import hashlib
import json

parser=argparse.ArgumentParser()
parser.add_argument('--source-root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--fence-dir',type=Path)
args=parser.parse_args()
source=args.source_root.resolve();out=args.out.resolve();fence=(args.fence_dir or source/'src').resolve()
out.mkdir(parents=True,exist_ok=True)
pins=[]
for name in ['vi_cursor_guard.h','vi_cursor_write_hooks.inl','vi_native_clip.inl','vi_claim_native_fence.h','vi_claim_native_fence.inl']:
 original=(fence if name.startswith('vi_claim_native_fence') else source/'src')/name
 data=original.read_bytes();(out/name).write_bytes(data)
 pins.append({'path':str(original),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest().upper()})
core_path=source/'src/vinput_core.cpp';raw=core_path.read_bytes();core=raw.decode('utf-8').replace('\r\n','\n')
start=core.index('std::string ClaimGate(');end=core.index('\nbool ResolveXY(',start)
body=core[start:end];(out/'claim_gate.inl').write_text(body,encoding='utf-8',newline='\n')
pins.append({'path':str(core_path),'sha256':hashlib.sha256(raw).hexdigest().upper(),'extracted_claim_gate_sha256':hashlib.sha256(body.encode()).hexdigest().upper()})
(out/'extracted_pins.json').write_text(json.dumps({'scope':'pure-memory source extraction only; no execution','files':pins},indent=2)+'\n',encoding='utf-8')
print(json.dumps({'out':str(out),'files':len(pins)}))
