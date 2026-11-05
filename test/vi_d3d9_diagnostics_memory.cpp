// Actual selected production wrappers; lower COM/functions are finite fakes only.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <cstdio>
#include <string>
#include "actual_diagnostics.inl"
struct FakeObject{void** table;};void* table[140]{};FakeObject fixtureDevice{table};const IID fakeIID{};
unsigned passed=0,failed=0,nativeCalls[5]{},released=0;bool requiredHooks=true,originalExists=true,metadata=true,admit=true,reenter=false;HRESULT nativeResult=S_OK;
bool lowerEntryCounted=true;
void Check(bool ok,const char* name){if(ok)++passed;else{++failed;std::printf("FAIL %s\n",name);}}
HRESULT STDMETHODCALLTYPE Query(IDirect3DDevice9*,REFIID,void** out){*out=nullptr;return E_NOINTERFACE;}
ULONG STDMETHODCALLTYPE Release(IDirect3DDevice9*){++released;return 1;}
HRESULT STDMETHODCALLTYPE Creation(IDirect3DDevice9*,D3DDEVICE_CREATION_PARAMETERS* out){if(!metadata)return E_FAIL;*out={};return S_OK;}
HRESULT STDMETHODCALLTYPE Create(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9** out){++nativeCalls[0];*out=SUCCEEDED(nativeResult)?(IDirect3DDevice9*)&fixtureDevice:nullptr;return nativeResult;}
HRESULT STDMETHODCALLTYPE CreateEx(IDirect3D9Ex*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*,IDirect3DDevice9Ex** out){++nativeCalls[1];*out=SUCCEEDED(nativeResult)?(IDirect3DDevice9Ex*)&fixtureDevice:nullptr;return nativeResult;}
HRESULT STDMETHODCALLTYPE h_D9Reset(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE Reset(IDirect3DDevice9* self,D3DPRESENT_PARAMETERS* pp){++nativeCalls[2];lowerEntryCounted=lowerEntryCounted&&g_d9ObservedOperations[2].nativeCalls==nativeCalls[2];if(reenter){reenter=false;return h_D9Reset(self,pp);}return nativeResult;}
HRESULT STDMETHODCALLTYPE ResetEx(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*){++nativeCalls[3];return nativeResult;}
HRESULT STDMETHODCALLTYPE Additional(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9** out){++nativeCalls[4];*out=nullptr;return nativeResult;}
enum{D9_CREATE,D9_CREATEEX,D9_RESET,D9_RESETEX,D9_ADDITIONAL};
template<class F>F D9Original(void*,unsigned,unsigned kind){void* functions[5]={(void*)&Create,(void*)&CreateEx,(void*)&Reset,(void*)&ResetEx,(void*)&Additional};return originalExists?(F)functions[kind]:nullptr;}
bool D9Patch(void*,unsigned,unsigned,void*,const char*){return requiredHooks;}
template<class F>HRESULT D9Parameters(D3DPRESENT_PARAMETERS* pp,HWND,DWORD,F call){return admit?call(pp,false):D3DERR_INVALIDCALL;}
#define IID_IDirect3DDevice9Ex fakeIID
void D9ObserveDevice(IDirect3DDevice9*){} // optional capture observer tested by the dedicated Present fixture
bool D9Chain(IDirect3DSwapChain9*){return true;}
#include "actual_wrappers.inl"
#undef IID_IDirect3DDevice9Ex
#include "actual_json.inl"
int main(){
 table[0]=(void*)&Query;table[2]=(void*)&Release;table[9]=(void*)&Creation;
 Check(D9DiagnosticsJson().find("\"wrapper_entries\":null")!=std::string::npos,"unreached wrappers are unmeasured/null");
 D3DPRESENT_PARAMETERS pp{};IDirect3DDevice9* device=nullptr;IDirect3DDevice9Ex* ex=nullptr;IDirect3DSwapChain9* chain=nullptr;
 Check(h_D9Create(nullptr,1,D3DDEVTYPE_HAL,nullptr,0,&pp,&device)==S_OK&&device==(IDirect3DDevice9*)&fixtureDevice&&nativeCalls[0]==1,"CreateDevice native result/output unchanged");
 Check(h_D9CreateEx(nullptr,1,D3DDEVTYPE_HAL,nullptr,0,&pp,nullptr,&ex)==S_OK&&ex==(IDirect3DDevice9Ex*)&fixtureDevice&&nativeCalls[1]==1,"CreateDeviceEx native result/output unchanged");
 Check(h_D9Reset(device,&pp)==S_OK&&nativeCalls[2]==1,"Reset native result unchanged");Check(h_D9ResetEx(ex,&pp,nullptr)==S_OK&&nativeCalls[3]==1,"ResetEx native result unchanged");
 Check(h_D9Additional(device,&pp,&chain)==S_OK&&nativeCalls[4]==1&&chain==nullptr,"Additional native result/output unchanged");
 for(unsigned kind=0;kind<5;kind++)Check(g_d9ObservedOperations[kind].entries==1&&g_d9ObservedOperations[kind].nativeCalls==1&&g_d9ObservedOperations[kind].nativeOk==1&&g_d9ObservedOperations[kind].returned==1,"observed wrapper/native success separated per method");
 nativeResult=D3DERR_DEVICELOST;Check(h_D9Reset(device,&pp)==D3DERR_DEVICELOST&&nativeCalls[2]==2,"lost-device result not repaired or retried");Check(g_d9ObservedOperations[2].nativeLast==D3DERR_DEVICELOST&&g_d9ObservedOperations[2].nativeFailed==1&&g_d9ObservedOperations[2].failed==1,"lost-device diagnostic exact");
 nativeResult=S_OK;metadata=false;Check(h_D9Reset(device,&pp)==E_FAIL&&nativeCalls[2]==2,"metadata failure preserves no-native boundary");Check(g_d9ObservedOperations[2].returned==3&&g_d9ObservedOperations[2].nativeCalls==2,"metadata refusal not counted as native failure");metadata=true;
 admit=false;Check(h_D9Create(nullptr,1,D3DDEVTYPE_HAL,nullptr,0,&pp,&device)==D3DERR_INVALIDCALL&&nativeCalls[0]==1,"existing fullscreen/policy refusal still before native");Check(g_d9ObservedOperations[0].entries==2&&g_d9ObservedOperations[0].nativeCalls==1,"policy refusal distinct from lower result");admit=true;
 originalExists=false;Check(h_D9Reset((IDirect3DDevice9*)&fixtureDevice,&pp)==D3DERR_INVALIDCALL&&nativeCalls[2]==2,"missing original never called");Check(g_d9ObservedOperations[2].returned==4&&g_d9ObservedOperations[2].nativeCalls==2,"missing-original wrapper refusal recorded");originalExists=true;
 requiredHooks=false;Check(h_D9Create(nullptr,1,D3DDEVTYPE_HAL,nullptr,0,&pp,&device)==D3DERR_INVALIDCALL&&device==nullptr&&released==1&&nativeCalls[0]==2,"required existing hook failure preserves owned output release/refusal");Check(g_d9ObservedOperations[0].nativeOk==2&&g_d9ObservedOperations[0].failed==2,"successful lower create is separate from wrapper failure");requiredHooks=true;
 reenter=true;Check(h_D9Reset((IDirect3DDevice9*)&fixtureDevice,&pp)==S_OK&&nativeCalls[2]==4,"reentrant lower callback completes without new lock");Check(g_d9ObservedOperations[2].returned==6&&g_d9ObservedOperations[2].nativeCalls==4,"reentrant wrapper and lower returns counted separately");
 Check(D9DiagnosticsJson().find("negotiation counters do not establish rendered frames")!=std::string::npos,"negotiation diagnostics explicitly does not claim rendered frames");
 Check(lowerEntryCounted&&g_d9ObservedOperations[2].nativeReturns==4,"native entry is published before callback, returns counted separately");
 std::printf("{\"passed\":%u,\"failed\":%u,\"completed\":true,\"scope\":\"exact production wrappers/HRESULT storage/JSON; finite fabricated callbacks only\"}\n",passed,failed);return failed?1:0;
}
