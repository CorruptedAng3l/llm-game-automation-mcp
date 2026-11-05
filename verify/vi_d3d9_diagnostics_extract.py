from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,required=True);p.add_argument('--diagnostics-dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists()
header=a.source_root/'src/vi_d3d9windowed.h';core=a.source_root/'src/vinput_core.cpp';h=header.read_text(encoding='utf-8-sig');c=core.read_text(encoding='utf-8-sig');guarded='#include "vi_d3d9_diagnostics.inl"' in h;storage=a.diagnostics_dir/'vi_d3d9_diagnostics.inl';view=a.diagnostics_dir/'vi_d3d9_diagnostics_json.inl';pieces={'actual_diagnostics.inl':storage.read_text()}
wrappers='bool D9Device(IDirect3DDevice9*);\n'
for prefix in ['using D9Create=','using D9CreateEx=','using D9Reset=','using D9ResetEx=','using D9Additional=']+[f'HRESULT STDMETHODCALLTYPE {name}(' for name in ['h_D9Create','h_D9CreateEx','h_D9Reset','h_D9ResetEx','h_D9Additional']]+['bool D9Device(IDirect3DDevice9* device)']:
 lines=[line for line in h.splitlines() if line.startswith(prefix)];assert len(lines)==1,(prefix,len(lines));wrappers+=lines[0]+'\n'
pieces['actual_wrappers.inl']=wrappers
esc=c[c.index('std::string JEsc('):c.index('\n}\n',c.index('std::string JEsc('))+3];jout=c[c.index('struct JOut {'):c.index('\n};',c.index('struct JOut {'))+4];pieces['actual_json.inl']=esc+jout+'\n'+(view.read_text() if guarded else 'std::string D9DiagnosticsJson(){return "{}";}\n')
sha=lambda b:hashlib.sha256(b).hexdigest().upper();a.out.mkdir(parents=True)
for name,value in pieces.items():(a.out/name).write_text(value,encoding='utf-8')
(a.out/'sourcepins.json').write_text(json.dumps({'guarded':guarded,'inputs':{str(f.resolve()):sha(f.read_bytes()) for f in [header,core,storage,view]},'pieces':{name:sha(value.encode()) for name,value in pieces.items()},'scope':'actual selected wrappers/storage/JSON; old only shares passive storage plus absent-status adapter; fullscreen parameters are a controlled stub, not a tested implementation'},indent=2)+'\n')
