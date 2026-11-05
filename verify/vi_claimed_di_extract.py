"""Extract actual production bodies for the no-desktop claimed-DI memory test."""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--source-root',required=True,type=Path);p.add_argument('--fence-dir',required=True,type=Path);p.add_argument('--test-file',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest().upper()
if a.out.exists():raise SystemExit('output must be fresh')
core=a.source_root/'src/vinput_core.cpp';text=core.read_text(encoding='utf-8-sig');guarded='#include "vi_claimed_di.inl"' in text
inputs={str(core.resolve()):sha(core.read_bytes()),str(a.test_file.resolve()):sha(a.test_file.read_bytes())}
def span(start,end):
 if text.count(start)!=1:raise SystemExit('source signature missing or ambiguous: '+start)
 i=text.index(start);j=text.index(end,i);return text[i:j].rstrip()+'\n'
pieces={'actual_devtype.inl':span('int DevType(void* self) {','#include "vi_claimed_di.inl"' if guarded else 'HRESULT DIAcquireImpl(').encode(),'actual_acquire.inl':span('HRESULT DIAcquireImpl(','// DirectInput polls').encode()}
i=text.index('HRESULT STDMETHODCALLTYPE h_DIAcquire(');pieces['actual_known_wrapper.inl']=text[i:text.index('\n',i)].encode()+b'\n'
if guarded:
 for name,out in [('vi_claimed_di.inl','actual_policy.inl'),('vi_claimed_di_hooks.inl','actual_hooks.inl')]:
  file=a.source_root/'src'/name;b=file.read_bytes();inputs[str(file.resolve())]=sha(b);pieces[out]=b
else:
 pieces['actual_policy.inl']=b'// Old source has no SetCooperativeLevel hook: invoke its unchanged original.\nHRESULT ClaimedDiSetCoopImpl(PFN_DICOOP original,void* self,HWND window,DWORD flags){return original(self,window,flags);}\n'
 pieces['actual_hooks.inl']=b'// Old source has no claimed-mouse probes/typed registry.\nvoid TryClaimedDi8(){}\nvoid TryClaimedDiLegacy(){}\nbool ClaimDiInstallEntry(void*,bool){return false;}\nbool ClaimDiHookKnownAcquire(const char* name,void* target,void* detour,PFN_DIACQ* original){return HookAddr(name,target,detour,(void**)original);}\n'
if '#include "vi_pad_api_census.h"' in text:
 file=a.source_root/'src/vi_pad_api_census.h';b=file.read_bytes();inputs[str(file.resolve())]=sha(b);pieces['vi_pad_api_census.h']=b
for name in ['vi_claim_native_fence.h','vi_claim_native_fence.inl']:
 file=a.fence_dir/name;b=file.read_bytes();inputs[str(file.resolve())]=sha(b);pieces[name]=b
# No alternate source, copied implementation, implicit peer or DLL load fallback.
a.out.mkdir(parents=True)
for name,b in pieces.items():(a.out/name).write_bytes(b)
(a.out/'sourcepins.json').write_text(json.dumps({'guarded_source':guarded,'inputs':inputs,'extracted':{k:sha(v) for k,v in pieces.items()},'baseline_adapters':'only absent cooperative hook/probes route unchanged original or no-op installation; policy/Acquire/DevType/shared fence are never reimplemented'},indent=2)+'\n')
print(json.dumps({'guarded_source':guarded,'sourcepins':str(a.out/'sourcepins.json')}))
