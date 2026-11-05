from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--source-root',required=True,type=Path);p.add_argument('--census-dir',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
if a.out.exists():raise SystemExit('output must be fresh')
sha=lambda b:hashlib.sha256(b).hexdigest().upper()
def function(s,needle):
 assert s.count(needle)==1,(needle,s.count(needle))
 start=s.index(needle);i=s.index('{',start);depth=0;state='code'
 for j in range(i,len(s)):
  c=s[j];n=s[j+1:j+2];prev=s[j-1:j]
  if state=='line':
   if c=='\n':state='code'
   continue
  if state=='block':
   if prev=='*' and c=='/':state='code'
   continue
  if state in ['"',"'"]:
   if c==state:
    k=j-1
    while k>=0 and s[k]=='\\':k-=1
    if (j-1-k)%2==0:state='code'
   continue
  if c=='/' and n=='/':state='line';continue
  if c=='/' and n=='*':state='block';continue
  if c in ['"',"'"]:state=c;continue
  if c=='{':depth+=1
  if c=='}':
   depth-=1
   if depth==0:return s[start:j+1]+'\n'
 raise ValueError(needle)
source=a.source_root/'src/vinput_core.cpp';text=source.read_text(encoding='utf-8-sig');guarded='#include "vi_pad_api_census.h"' in text
inputs={str(source.resolve()):sha(source.read_bytes())}
pieces={}
for name in ['vi_pad_api_census.h','vi_pad_api_census_json.inl']:
 file=a.census_dir/name;b=file.read_bytes();pieces[name]=b;inputs[str(file.resolve())]=sha(b)
file=a.source_root/'src/vi_pad_readiness_state.inl';pieces['actual_pad_state.inl']=file.read_bytes();inputs[str(file.resolve())]=sha(file.read_bytes())
pieces['actual_json.inl']=(function(text,'std::string JEsc(')+text[text.index('struct JOut {'):text.index('\n};',text.index('struct JOut {'))+4]+'\n').encode()
for name,needle in [('actual_xgs.inl','DWORD XGSCommon('),('actual_raw_data.inl','UINT WINAPI h_GetRawInputData('),('actual_registration.inl','BOOL WINAPI h_RegisterRawInputDevices('),('actual_devtype.inl','int DevType(void* self) {')]:pieces[name]=function(text,needle).encode()
for name,needle,end in [('actual_di_state_prefix.inl','HRESULT DIGetDeviceStateImpl(','    const bool kbd='),('actual_di_buffer_prefix.inl','HRESULT DIGetDeviceDataImpl(','    if(type==1)C.di_data_kbd++')]:
 start=text.index(needle);stop=text.index(end,start);pieces[name]=(text[start:stop]+'    static_cast<void>(mask);++modelOutOfScope;return NativeFailure;\n}\n').encode()
if guarded:
 file=a.source_root/'src/vi_pad_api_census_exports.inl';b=file.read_text(encoding='utf-8-sig');inputs[str(file.resolve())]=sha(file.read_bytes());pieces['actual_exports.inl']=b.replace('#include <Xinput.h>\n','').replace('#include <joystickapi.h>\n','').encode()
else:
 pieces['actual_exports.inl']=b'// Old flat exports are unhooked; no substitute counter policy.\nstd::atomic<void*> g_padApiOriginal[5]{},g_padApiTarget[5]{};\nstd::atomic<unsigned> g_padApiExportState[5]{},g_padApiExportAlias[5]{};\nvoid TryHookPadApiCensus(){}\n'
a.out.mkdir(parents=True)
for name,b in pieces.items():(a.out/name).write_bytes(b)
(a.out/'sourcepins.json').write_text(json.dumps({'guarded_source':guarded,'inputs':inputs,'pieces':{name:sha(b) for name,b in pieces.items()},'scope':'actual changed bodies; no-op absent export adapter; exact DI proven-other prefix only, sentinel must never be reached'},indent=2)+'\n')
print(json.dumps({'guarded_source':guarded,'sourcepins':str(a.out/'sourcepins.json')}))
