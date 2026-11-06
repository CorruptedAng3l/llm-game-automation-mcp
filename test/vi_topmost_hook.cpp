// Extracted actual h_SetWindowPos body, fabricated endpoints only. Never calls
// user32, a real HWND, a native writer or foreground API. The separate native
// policy fixture proves the helper itself; this proves wrapper composition.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
struct TestModel {bool helper=true,block=true;BOOL result=TRUE;unsigned calls=0,helperCalls=0,handled=0,logs=0;HWND window=nullptr,after=nullptr,foreground=(HWND)(ULONG_PTR)301;int x=0,y=0,width=0,height=0;UINT flags=0;};
TestModel model;HWND g_hwnd=(HWND)(ULONG_PTR)101;struct {std::atomic<bool> topmostKeep{false};} g_cfg;
bool IsOurWindow(HWND window){return window==g_hwnd||window==(HWND)(ULONG_PTR)102;}
HWND FabricatedParent(HWND){return nullptr;}
bool RealFg(){return model.foreground==g_hwnd;}
bool BlockForegroundActivation(){return model.block;}
void Log(const char*,...){++model.logs;}
BOOL WINAPI FinitePosition(HWND window,HWND after,int x,int y,int width,int height,UINT flags){++model.calls;model.window=window;model.after=after;model.x=x;model.y=y;model.width=width;model.height=height;model.flags=flags;return model.result;}
BOOL(WINAPI* o_SetWindowPos)(HWND,HWND,int,int,int,int,UINT)=FinitePosition;
bool TopmostSetWindowPos(HWND window,HWND after,int x,int y,int width,int height,UINT flags,BOOL& result){
 ++model.helperCalls;
 // A finite delegated-helper endpoint. Its actual owned-scope/census/native
 // implementation has its own100 native controls; no policy is asserted here.
 if(!model.helper||!model.block||window!=g_hwnd||g_cfg.topmostKeep||after!=HWND_TOPMOST||(flags&SWP_NOZORDER))return false;
 ++model.handled;result=o_SetWindowPos(window,HWND_BOTTOM,x,y,width,height,(flags&~SWP_NOZORDER)|SWP_NOACTIVATE|SWP_NOOWNERZORDER);return true;
}
#define GetParent FabricatedParent
#include "actual_topmost_hook.inl"
#undef GetParent
unsigned passed=0,failed=0;
void Check(bool good,const char* name){printf("%s %s\n",good?"PASS":"FAIL",name);if(good)++passed;else ++failed;}
void Reset(bool keep=false){model=TestModel{};g_cfg.topmostKeep=keep;}
bool Exact(HWND window,HWND after,UINT flags){return model.calls==1&&model.window==window&&model.after==after&&model.flags==flags&&model.x==11&&model.y==17&&model.width==320&&model.height==240&&model.foreground==(HWND)(ULONG_PTR)301;}
BOOL Call(HWND window,HWND after,UINT flags=0){return h_SetWindowPos(window,after,11,17,320,240,flags);}
int main(){
 Reset();Check(Call(g_hwnd,HWND_TOPMOST)==TRUE&&Exact(g_hwnd,HWND_BOTTOM,SWP_NOACTIVATE|SWP_NOOWNERZORDER),"default validated helper result goes to one bottom/nonactivating endpoint and preserves caller geometry");Check(model.helperCalls==1&&model.handled==1,"actual wrapper delegates exactly once before original z-order guard");
 Reset();model.result=FALSE;Check(Call(g_hwnd,HWND_TOPMOST)==FALSE&&model.calls==1,"helper native failure is forwarded exactly with no replay");Check(model.handled==1&&model.after==HWND_BOTTOM,"failed handled helper never falls through into a second topmost call");
 Reset(true);Check(Call(g_hwnd,HWND_TOPMOST)==TRUE&&Exact(g_hwnd,HWND_TOPMOST,SWP_NOACTIVATE),"explicit keep adopted target preserves TOPMOST without adding NOZORDER while still preventing activation");
 Reset(true);Check(Call(g_hwnd,HWND_TOP)==TRUE&&Exact(g_hwnd,HWND_TOP,SWP_NOACTIVATE|SWP_NOZORDER),"keep leaves ordinary TOP background elevation blocked");
 Reset(true);const HWND related=(HWND)(ULONG_PTR)102;Check(Call(related,HWND_TOPMOST)==TRUE&&Exact(related,HWND_TOPMOST,SWP_NOACTIVATE|SWP_NOZORDER),"keep exception never applies to an unadopted same-PID window");
 Reset();const HWND foreign=(HWND)(ULONG_PTR)103;Check(Call(foreign,HWND_TOPMOST)==TRUE&&Exact(foreign,HWND_TOPMOST,0),"foreign unrelated HWND retains the original caller request without a new helper operation");
 Reset();Check(Call(g_hwnd,HWND_TOPMOST,SWP_NOZORDER)==TRUE&&Exact(g_hwnd,HWND_TOPMOST,SWP_NOZORDER|SWP_NOACTIVATE)&&model.handled==0,"caller NOZORDER is retained and cannot become a helper demotion");
 Reset(true);Check(Call(g_hwnd,HWND_TOPMOST,SWP_NOZORDER|SWP_NOSIZE)==TRUE&&Exact(g_hwnd,HWND_TOPMOST,SWP_NOZORDER|SWP_NOSIZE|SWP_NOACTIVATE),"keep never removes caller NOZORDER or NOSIZE");
 Reset();model.helper=false;Check(Call(g_hwnd,HWND_TOPMOST)==TRUE&&Exact(g_hwnd,HWND_TOPMOST,SWP_NOZORDER|SWP_NOACTIVATE),"unconfirmed helper eligibility retains the original background topmost guard");
 Reset();Check(Call(g_hwnd,HWND_BOTTOM,SWP_NOMOVE)==TRUE&&Exact(g_hwnd,HWND_BOTTOM,SWP_NOMOVE|SWP_NOACTIVATE),"ordinary bottom call retains caller flags plus nonactivation only");
 Reset();model.foreground=g_hwnd;model.block=false;Check(Call(g_hwnd,HWND_TOPMOST)==TRUE&&model.calls==1&&model.handled==0&&model.after==HWND_TOPMOST&&model.flags==0&&model.foreground==g_hwnd,"foreground owner request stays on original one-call path without foreground mutation");
 Reset();Check(Call(foreign,HWND_TOP,SWP_NOACTIVATE|SWP_NOSIZE)==TRUE&&Exact(foreign,HWND_TOP,SWP_NOACTIVATE|SWP_NOSIZE),"unrelated explicit nonactivation flags are preserved exactly");
 Check(o_SetWindowPos==FinitePosition,"all underlying calls terminate at the fabricated memory endpoint");
 printf("actual topmost hook: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
