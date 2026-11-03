import re,pathlib,argparse
parser=argparse.ArgumentParser();parser.add_argument('--sdk',default=r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\ddraw.h');parser.add_argument('--check',action='store_true');options=parser.parse_args();sdk=pathlib.Path(options.sdk).read_text()
names=['IDirectDraw','IDirectDraw2','IDirectDraw4','IDirectDraw7','IDirectDrawSurface','IDirectDrawSurface2','IDirectDrawSurface3','IDirectDrawSurface4','IDirectDrawSurface7']
out=['// Generated forwarding boilerplate from the installed Windows SDK; policy is handwritten.']
for k,name in enumerate(names,1):
 block=re.search(r'DECLARE_INTERFACE_\(\s*'+name+r'\s*,\s*IUnknown\s*\)\s*\{(.*?)\n\};',sdk,re.S).group(1)
 out.append(f'struct DdWrap{k} final : {name}, DdProxy {{ using Native={name}; Native* real; DdWrap{k}(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,{k}),real(n){{}} ~DdWrap{k}(){{real->Release();}}')
 for m in re.finditer(r'STDMETHOD(?:_\(([^,]+),\s*(\w+)\)|\((\w+)\))\s*\(\s*THIS(_?)(.*?)\)\s*PURE;',block,re.S):
  ret=m[1] or 'HRESULT'; method=m[2] or m[3]; raw=m[5].strip(); types=[]
  if raw:
   for i,p in enumerate(raw.split(',')):
    p=re.sub(r'\bFAR\b','',p).strip()
    # Only QueryInterface's SDK arguments carry names.
    if method=='QueryInterface':p=['REFIID','LPVOID *'][i]
    types.append(p)
  params=', '.join(t+f' p{i}' for i,t in enumerate(types));args=', '.join(f'p{i}' for i in range(len(types)))
  unwrapped=', '.join((f'DdUnwrap(p{i})' if t.strip().startswith('LPDIRECTDRAW') and '*' not in t else f'p{i}') for i,t in enumerate(types))
  body=f'return real->{method}({unwrapped});'
  if method=='QueryInterface':body='return DdQuery(group,p0,p1);'
  elif method=='AddRef':body='return RefAdd();'
  elif method=='Release':body=f'auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;'
  elif name.startswith('IDirectDrawSurface'):
   custom={'Blt':'return DdSurfaceWrite(*this,[&]{return real->Blt(p0,DdUnwrap(p1),p2,p3,p4);});',
    'BltFast':'return DdSurfaceWrite(*this,[&]{return real->BltFast(p0,p1,DdUnwrap(p2),p3,p4);});',
    'Unlock':'return DdSurfaceWrite(*this,[&]{return real->Unlock(p0);});',
    'ReleaseDC':'return DdSurfaceWrite(*this,[&]{return real->ReleaseDC(p0);});',
    'Flip':'return DdFlip(*this,p0,p1);','GetAttachedSurface':'return DdAttached(*this,p0,p1);',
    'GetSurfaceDesc':'return DdDescription(*this,p0);','GetCaps':'return DdCaps(*this,p0);',
    'GetDDInterface':'return DdParent(*this,p0);','IsLost':'return DdLost(group->surface)?DDERR_SURFACELOST:real->IsLost();',
    'Lock':'return DdLock(*this,p0,p1,p2,p3);',
    'GetDC':'if(DdLost(group->surface))return DDERR_SURFACELOST;return real->GetDC(p0);',
    'SetPalette':'return DdPalette(*this,p0);','Restore':'return DdLost(group->surface)?DDERR_WRONGMODE:real->Restore();'}
   if method in custom:body=custom[method]
   elif method in ['EnumAttachedSurfaces','EnumOverlayZOrders','BltBatch','AddAttachedSurface','DeleteAttachedSurface','UpdateOverlay','UpdateOverlayZOrder','Initialize','SetSurfaceDesc']:
    body='return DDERR_UNSUPPORTED;'
  else:
   custom={'SetCooperativeLevel':'return DdCoop(*this,p0,p1);','SetDisplayMode':f'return DdSetMode(*this,p0,p1,p2,{"0" if k==1 else "p3"},[&]{{return real->SetDisplayMode({args});}});',
    'GetDisplayMode':'return DdGetMode(*this,p0);','RestoreDisplayMode':'return DdRestoreMode(*this);','CreateSurface':'return DdCreateSurface(*this,p0,p1,p2);',
    'GetGDISurface':'return DdGdiSurface(*this,p0);','DuplicateSurface':'return DDERR_UNSUPPORTED;',
    'EnumSurfaces':'return DDERR_UNSUPPORTED;','GetSurfaceFromDC':'return DDERR_UNSUPPORTED;',
    'StartModeTest':'return g_cfg.nofullscreen?DDERR_UNSUPPORTED:real->StartModeTest(p0,p1,p2);'}
   if method in custom:body=custom[method]
  # Unused names in refused methods are deliberately unnamed to satisfy /W4.
  if body=='return DDERR_UNSUPPORTED;':params=', '.join(types)
  out.append(f' {ret} STDMETHODCALLTYPE {method}({params}) override {{{body}}}')
 out.append('};')
out.append('IUnknown* DdNewProxy(int kind,IUnknown* native,const std::shared_ptr<DdGroup>& group){switch(kind){')
for k,name in enumerate(names,1):out.append(f'case {k}:return new DdWrap{k}(({name}*)native,group);')
out.append('default:return nullptr;}}')
target=pathlib.Path(__file__).resolve().parents[1]/'src/vi_ddraw_forward.inl'
result='\n'.join(out)+'\n'
if options.check:
 assert target.read_text()==result,'generated DirectDraw forwarding file differs'
 print('PASS DirectDraw forwarding file matches the Windows SDK generator')
else:target.write_text(result)
