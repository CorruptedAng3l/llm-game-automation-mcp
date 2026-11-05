// Official SDK constant/type proof only. No graphics object or API call.
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#include <windows.h>
#include <d3d9.h>
#include <cstddef>
#include <cstdio>
#include <type_traits>
using Present=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using PresentEx=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using Chain=HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
static_assert(std::is_same<decltype(IDirect3DDevice9Vtbl::Present),Present>::value,"SDK device Present ABI");
static_assert(std::is_same<decltype(IDirect3DDevice9ExVtbl::PresentEx),PresentEx>::value,"SDK PresentEx ABI");
static_assert(std::is_same<decltype(IDirect3DSwapChain9Vtbl::Present),Chain>::value,"SDK chain Present ABI");
static_assert(offsetof(IDirect3DDevice9Vtbl,Present)==17*sizeof(void*),"SDK named Present slot");
static_assert(offsetof(IDirect3DDevice9ExVtbl,PresentEx)==121*sizeof(void*),"SDK named PresentEx slot");
static_assert(offsetof(IDirect3DSwapChain9Vtbl,Present)==3*sizeof(void*),"SDK named chain Present slot");
static_assert(offsetof(IDirect3DDevice9Vtbl,GetSwapChain)==14*sizeof(void*),"SDK named GetSwapChain slot");
static_assert(offsetof(IDirect3DDevice9Vtbl,GetCreationParameters)==9*sizeof(void*),"SDK named creation metadata slot");
static_assert(offsetof(IDirect3DDevice9Vtbl,Reset)==16*sizeof(void*),"SDK named Reset slot");
static_assert(offsetof(IDirect3DDevice9ExVtbl,ResetEx)==132*sizeof(void*),"SDK named ResetEx slot");
static_assert(offsetof(IDirect3D9Vtbl,CreateDevice)==16*sizeof(void*),"SDK named CreateDevice slot");
static_assert(offsetof(IDirect3D9ExVtbl,CreateDeviceEx)==20*sizeof(void*),"SDK named CreateDeviceEx slot");
static_assert(offsetof(IDirect3DDevice9Vtbl,CreateAdditionalSwapChain)==13*sizeof(void*),"SDK named additional chain slot");
int main(){std::puts("{\"passed\":13,\"failed\":0,\"completed\":true,\"present\":17,\"present_ex\":121,\"chain_present\":3,\"scope\":\"official SDK constants and types only; no graphics/device API\"}");}
