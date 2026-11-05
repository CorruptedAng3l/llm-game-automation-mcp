// Real retained own hidden HWNDs; production Present/adoption/queued-show paths.
// The Present provider is synthetic (no GPU). All desktop writers are terminal,
// except one explicitly armed, exact own offscreen ShowWindow(SW_SHOWNA).
#include <vinput_core.cpp>
#undef MH_Initialize
#undef MH_CreateHook
#undef MH_EnableHook
extern "C" MH_STATUS WINAPI MH_Initialize(void);
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
#include "vi_r2_spies.h"
#include "vi_d3d9_fixture_geometry.h"
#include "vi_launch_ack.h"
#include "vi_ready_json.h"
#include <climits>
static unsigned passed=0,failed=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);ok?++passed:++failed;}
struct OwnedWindow{HWND window=nullptr;DWORD pid=0,thread=0;ULONG_PTR cookie=0;};
static std::vector<OwnedWindow> owned;static const wchar_t* ownProperty=L"vi.hidden.fixture.owned.v1";
static HANDLE retained=nullptr;static unsigned long long processBirth=0;static RECT desktop{};static bool showArmed=false,noActivationArmed=false;
static HWND showWindow=nullptr;static capture_spy::WindowVisibility nativeShow=nullptr;static unsigned showObserved=0,showAllowed=0,showRefused=0,showAfterFailed=0,asyncObserved=0;
static unsigned long long Birth(HANDLE h){FILETIME b{},e{},k{},u{};if(!GetProcessTimes(h,&b,&e,&k,&u))return 0;ULARGE_INTEGER n{};n.LowPart=b.dwLowDateTime;n.HighPart=b.dwHighDateTime;return n.QuadPart;}
static bool ProcessOwned(){wchar_t image[32768]{};DWORD size=32768;return retained&&GetProcessId(retained)==GetCurrentProcessId()&&WaitForSingleObject(retained,0)==WAIT_TIMEOUT&&Birth(retained)==processBirth&&processBirth&&QueryFullProcessImageNameW(retained,0,image,&size)&&size&&image[0];}
static bool Own(HWND h){if(!ProcessOwned())return false;for(const auto& record:owned)if(record.window==h){DWORD pid=0;return IsWindow(h)&&GetWindowThreadProcessId(h,&pid)==record.thread&&pid==record.pid&&pid==GetCurrentProcessId()&&record.thread==GetCurrentThreadId()&&(ULONG_PTR)GetPropW(h,ownProperty)==record.cookie;}return false;}
static bool ForegroundOutside(){const HWND foreground=GetForegroundWindow();DWORD pid=0;if(foreground&&!GetWindowThreadProcessId(foreground,&pid))return false;return pid!=GetCurrentProcessId();}
static bool Offscreen(HWND h){RECT window{},client{};POINT origin{};if(!Own(h)||!GetWindowRect(h,&window)||!GetClientRect(h,&client)||client.right<1||client.bottom<1||client.right>8192||client.bottom>8192||!ClientToScreen(h,&origin)||(long long)origin.x+client.right>LONG_MAX||(long long)origin.y+client.bottom>LONG_MAX)return false;RECT absolute{origin.x,origin.y,origin.x+client.right,origin.y+client.bottom};return vi_d9_fixture_geometry::Outside(window,desktop)&&vi_d9_fixture_geometry::Outside(absolute,desktop);}
static BOOL WINAPI OwnShow(HWND h,int cmd){++showObserved;if(!showArmed||h!=showWindow||g_hiddenShow.scope.window!=h||!g_hiddenShow.callAttempted||!nativeShow||cmd!=SW_SHOWNA||!noActivationArmed||!g_explicitNoActivation||!Own(h)||!Offscreen(h)||!ForegroundOutside()){++showRefused;++capture_spy::writes;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}showArmed=false;const BOOL old=nativeShow(h,SW_SHOWNA);if(!Own(h)||!Offscreen(h)||!ForegroundOutside()){++showAfterFailed;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}++showAllowed;return old;}
static BOOL WINAPI OwnAsync(HWND,int){++asyncObserved;++capture_spy::writes;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static void WINAPI TerminalKey(BYTE,BYTE,DWORD,ULONG_PTR){++capture_spy::writes;}
static bool GuardIntact(){
 for(const auto& hook:capture_spy::hooks)if(!capture_spy::Reaches(hook.entry,hook.detour))return false;
 const char* names[]={"SetCursorPos","SetPhysicalCursorPos","ClipCursor","SendInput","mouse_event","SetForegroundWindow","BringWindowToTop","SetActiveWindow","SetFocus","SwitchToThisWindow","AttachThreadInput","SetCapture","ReleaseCapture","AllowSetForegroundWindow","LockSetForegroundWindow","SetWindowPos","ShowWindow","ShowWindowAsync","keybd_event"};
 void* detours[]={(void*)&capture_spy::Position,(void*)&capture_spy::Position,(void*)&capture_spy::Clip,(void*)&capture_spy::Send,(void*)&capture_spy::Mouse,(void*)&capture_spy::Foreground,(void*)&capture_spy::Foreground,(void*)&capture_spy::Focus,(void*)&capture_spy::Focus,(void*)&capture_spy::Switch,(void*)&capture_spy::Attach,(void*)&capture_spy::Capture,(void*)&capture_spy::Release,(void*)&capture_spy::Permission,(void*)&capture_spy::LockPermission,(void*)&capture_spy::WindowPos,(void*)&OwnShow,(void*)&OwnAsync,(void*)&TerminalKey};
 for(unsigned i=0;i<19;++i){void* entry=(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),names[i]);bool covered=false;for(const auto& hook:capture_spy::hooks)if(hook.entry==entry&&hook.detour==detours[i]&&capture_spy::Reaches(entry,detours[i]))covered=true;if(!covered)return false;}return true;
}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return m==WM_MOUSEACTIVATE?MA_NOACTIVATE:DefWindowProcW(h,m,w,l);}
static HWND CreateOwn(const wchar_t* name=L"vi.hidden.present.owned",DWORD style=WS_POPUP,DWORD extended=WS_EX_NOACTIVATE,HWND parent=nullptr){
 if(!ProcessOwned()||(style&WS_VISIBLE))return nullptr;HWND h=CreateWindowExW(extended,name,L"Hidden owned fixture",style,desktop.right+1000,desktop.top,640,480,parent,nullptr,GetModuleHandleW(nullptr),nullptr);if(!h)return nullptr;
 OwnedWindow record;record.window=h;record.pid=GetCurrentProcessId();record.thread=GetCurrentThreadId();record.cookie=(ULONG_PTR)owned.size()+1;owned.push_back(record);if(!SetPropW(h,ownProperty,(HANDLE)record.cookie)||!Own(h)||((style&WS_CHILD)==0&&parent!=HWND_MESSAGE&&!Offscreen(h))){Check(false,"new owned window identity/shape");return nullptr;}return h;
}
static void Pump(){const auto deadline=GetTickCount64()+500;MSG m{};while(GetTickCount64()<deadline&&PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT)break;TranslateMessage(&m);DispatchMessageW(&m);}}
static void ResetSelection(){g_hwndForced=false;WndBindingDetachIfTop(g_hwnd);g_hwnd=nullptr;g_origWndProc=nullptr;g_adoptedAsHidden=false;g_hiddenShow={};g_ownerHolding=false;g_userIntent.SetTarget(nullptr,g_pid);}
// A synthetic chain provides only GetPresentParameters. No graphics API runs.
struct Chain final:IDirect3DSwapChain9{
 HWND window;explicit Chain(HWND h):window(h){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}ULONG STDMETHODCALLTYPE AddRef()override{return 1;}ULONG STDMETHODCALLTYPE Release()override{return 1;}
 HRESULT STDMETHODCALLTYPE Present(const RECT*,const RECT*,HWND,const RGNDATA*,DWORD)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT,D3DBACKBUFFER_TYPE,IDirect3DSurface9**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS* p)override{*p={};p->Windowed=TRUE;p->hDeviceWindow=window;return S_OK;}
};
static void Present(HWND window,HRESULT hr=S_OK){Chain chain(window);D9PresentFrame(nullptr,&chain,nullptr,[&]{return hr;});}
static JObj Json(const std::string& text){JObj value;vi_ready_json::Reader reader{text,0,{},128};const bool valid=reader.Parse();Check(valid,"typed nested native receipt parses");if(valid)for(const auto& field:reader.fields){bool flag=false;if(reader.Bool(field.first.c_str(),flag))value[field.first]=JV{3,{},0,flag};}return value;}
static std::string Show(HWND h,const char* owner="fixture"){char text[256];sprintf_s(text,"{\"cmd\":\"show-hidden\",\"hwnd\":\"0x%llX\",\"owner\":\"%s\"}",(unsigned long long)(ULONG_PTR)h,owner);return Exec(text);}
int main(int argc,char** argv){
 if(argc!=2||strcmp(argv[1],"--await-start"))return 2;char line[8]{};if(!fgets(line,sizeof(line),stdin)||strcmp(line,"RUN\n")){fprintf(stderr,"prerequisite: startup RUN unavailable\n");return 111;}
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);if(!vi_launch_ack::ArmDeadline())return 111;
 retained=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,GetCurrentProcessId());processBirth=Birth(retained);if(!ProcessOwned()){fprintf(stderr,"prerequisite: retained self identity unavailable %lu\n",GetLastError());return 111;}
 const auto x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);if(w<1||h<1||(long long)x+w+1640>LONG_MAX||(long long)y+h>LONG_MAX)return 111;desktop={x,y,x+w,y+h};
 if(!capture_spy::Install(&capture_spy::WindowPos,&OwnShow,&OwnAsync)||!capture_spy::Add("keybd_event",(void*)&TerminalKey)){fprintf(stderr,"prerequisite: terminal spy installation unavailable (%zu installed) %lu\n",capture_spy::hooks.size(),GetLastError());return 111;}
 for(const auto& hook:capture_spy::hooks)if(hook.detour==(void*)&OwnShow)nativeShow=(capture_spy::WindowVisibility)hook.original;
 noActivationArmed=vi_launch_ack::ArmNoActivation();if(!nativeShow||!noActivationArmed||!GuardIntact()){fprintf(stderr,"prerequisite: show trampoline=%u CBT=%u intact=%u %lu\n",nativeShow?1u:0u,noActivationArmed?1u:0u,GuardIntact()?1u:0u,GetLastError());return 111;}
 g_pid=GetCurrentProcessId();g_t0=GetTickCount();g_cfg.autoactivate=false;g_capReq=0;g_capEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);o_ShowWindow=&ShowWindow;
 g_hiddenShowMessage=RegisterWindowMessageW(L"vinput.explicit.show.hidden.v1");g_adoptionActivationMessage=RegisterWindowMessageW(L"vinput.virtual.activation.adoption.v1");g_adoptionActivationSafe=g_hiddenShowMessage&&g_adoptionActivationMessage;
 WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Proc;cls.lpszClassName=L"vi.hidden.present.owned";if(!RegisterClassW(&cls))return 111;cls.lpszClassName=L"vinput.d3d9.hidden.probe";if(!RegisterClassW(&cls))return 111;
 Check(CaptureScopeFailure("capture_target_unavailable").find("no_window_adopted")!=std::string::npos,"PT6 no-window capture identifies missing adoption");
 const HWND first=CreateOwn();if(!first)return 111;Check(!IsWindowVisible(first)&&Offscreen(first),"owned exact-lifetime window starts hidden and fully offscreen");
 const auto startShows=showObserved;Check(!AutoAdoptWindow()&&!g_hwnd,"hidden enumeration alone never becomes presentation/adoption proof");Present(first,D3DERR_DEVICELOST);Check(!AutoAdoptWindow()&&!g_hwnd,"failed synthetic native Present never authorizes adoption");
 const HWND probe=CreateOwn(L"vinput.d3d9.hidden.probe");const HWND tool=CreateOwn(L"vi.hidden.present.owned",WS_POPUP,WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);const HWND child=CreateOwn(L"vi.hidden.present.owned",WS_CHILD,0,first);const HWND message=CreateOwn(L"vi.hidden.present.owned",0,0,HWND_MESSAGE);if(!probe||!tool||!child||!message)return 111;
 Present(probe);Present(tool);Present(child);Present(message);for(HWND item:{first,probe,tool,child,message}){const auto p=PresentWindowProof(item);printf("CONTROL hwnd=%p adopted=%p parent=%p actual_parent=%p desktop=%p top=%u child=%u message=%u tool=%u dummy=%u visible=%u cookie=%llu\n",item,g_hwnd,GetParent(item),GetAncestor(item,GA_PARENT),GetDesktopWindow(),p.top?1u:0u,p.child?1u:0u,p.messageOnly?1u:0u,p.tool?1u:0u,p.dummy?1u:0u,p.visible?1u:0u,(unsigned long long)p.cookie);}Check(!AutoAdoptWindow()&&!g_hwnd,"probe/tool/child/message-only presenters cannot be adopted");
 Present(first);Check(AutoAdoptWindow()&&g_hwnd==first&&WndBindingReady(first)&&g_adoptedAsHidden,"successful production D9 Present metadata adopts exact hidden target binding");Check(!IsWindowVisible(first)&&showObserved==startShows&&!CanRefreshAdoptedWindow(first),"default adoption makes no show call or automatic activation refresh");
 Check(CompletedPresentAt(first,vi_qpc::Now())==1&&CompletedPresentAt(first,0)==0&&CompletedPresentAt(tool,vi_qpc::Now())==0,"edge frame witness counts only a confirmed successful own-window Present at or before its QPC");
 Present(first,D3DERR_DEVICELOST);Present(first,S_FALSE);Check(CompletedPresentAt(first,vi_qpc::Now())==1,"failed and non-S_OK native Presents never advance completed frame evidence");
 CaptureScope mappedScope;Check(CaptureScopeTake(mappedScope),"actual adopted window provides a capture-map lifetime scope");g_savedCaptureMap={true,320,240,50,60,200,100,mappedScope};g_captureMapSerial=10;
 const auto getter=Exec("{\"cmd\":\"capture-map\"}");vi_ready_json::Reader mapRead{getter,0,{}};Check(mapRead.Parse()&&mapRead.fields.at("capture_map_id").raw=="\"10\"","actual valid-window map getter exposes persisted identity to a fresh client");
 JOut resize;resize.str("cmd","capture-map-resize");resize.str("capture_map_id","10");resize.str("expected_capture_scope",CaptureScopeJson(mappedScope));resize.num("image_w",160);resize.num("image_h",120);const auto changed=Exec(resize.done());vi_ready_json::Reader mapWrite{changed,0,{}};bool resized=false;Check(mapWrite.Parse()&&mapWrite.Bool("ok",resized)&&resized&&g_captureMapSerial==11&&g_savedCaptureMap.w==160&&g_savedCaptureMap.h==120&&g_savedCaptureMap.ox==50&&g_savedCaptureMap.cw==200,"actual valid-window resize CAS changes only image dimensions");
 auto native=Json(Exec("{\"cmd\":\"presentation-status\"}"));Check(JBool(native,"window_adopted",false)&&JBool(native,"window_hidden",false),"lightweight native receipt identifies adopted hidden target");
 Check(JBool(Json(Exec("{\"cmd\":\"claim\",\"owner\":\"fixture\",\"ttl\":30}")),"ok",false),"explicit owner claim admitted");
 auto refused=Json(Show(first,"other"));Check(!JBool(refused,"ok",true)&&!g_hiddenShow.pending,"different claim owner cannot queue visibility");
 refused=Json(Exec("{\"cmd\":\"show-hidden\",\"hwnd\":\"0x1junk\",\"owner\":\"fixture\"}"));Check(!JBool(refused,"ok",true),"malformed exact HWND is refused");
 auto queued=Json(Show(first));Check(JBool(queued,"ok",false)&&g_hiddenShow.pending&&!g_hiddenShow.callAttempted&&!IsWindowVisible(first),"explicit show admission returns pending without synchronously changing visibility");const auto previousOwner=g_ownerSeq.load();++g_ownerSeq;Pump();Check(!g_hiddenShow.pending&&g_hiddenShow.reason=="owner_activity"&&showObserved==startShows,"new owner activity cancels queued show without calling native visibility");g_ownerSeq=previousOwner;
 queued=Json(Show(first));Check(JBool(queued,"ok",false)&&g_hiddenShow.pending,"fresh exact request can queue after cancelled ticket");g_hiddenShow.expires=HousekeepingNow();Pump();Check(!g_hiddenShow.pending&&g_hiddenShow.reason=="expired_without_dispatch"&&showObserved==startShows,"delayed UI dispatch expires without show or replay");
 queued=Json(Show(first));Check(JBool(queued,"ok",false),"one explicit guarded visibility request accepted");showWindow=first;showArmed=true;Pump();showArmed=false;showWindow=nullptr;native=Json(Exec("{\"cmd\":\"presentation-status\"}"));Check(g_hiddenShow.completed&&g_hiddenShow.callAttempted&&showAllowed==1&&IsWindowVisible(first)&&!JBool(native,"window_hidden",true)&&Offscreen(first),"only exact own offscreen SW_SHOWNA delegates; visibility observed independently");
 const auto allowed=showAllowed;OwnShow(first,SW_SHOW);OwnShow(nullptr,SW_SHOWNA);OwnAsync(first,SW_SHOWNA);Check(showAllowed==allowed&&showRefused>=2&&asyncObserved>=1,"all unarmed/foreign/activating/async visibility requests stay terminal");
 ResetSelection();const HWND a=CreateOwn(),b=CreateOwn();if(!a||!b)return 111;{std::lock_guard<std::mutex> lock(g_presentWindowMx);g_presentWindows.clear();}Present(a);Present(b);Check(!AdoptPresentedWindow(HiddenPresentedWindow().evidence)&&!g_hwnd&&HiddenPresentedWindow().selected.count==2,"two hidden Present lifetimes refuse ambiguity");
 {std::lock_guard<std::mutex> lock(g_presentWindowMx);g_presentWindows.clear();}const HWND fresh=CreateOwn();if(!fresh)return 111;Present(fresh);auto evidence=HiddenPresentedWindow().evidence;Check(AdoptPresentedWindow(evidence)&&g_hwnd==fresh,"unique genuinely fresh HWND adopts after isolated ambiguous-fixture reset; old cookies are never erased or reused");
 const auto lifetime=GetPropW(fresh,kWndProcLifetimeProperty);RemovePropW(fresh,kWndProcLifetimeProperty);refused=Json(Show(fresh));Check(!JBool(refused,"ok",true)&&!g_hiddenShow.pending&&showAllowed==1,"lost protected binding cannot queue visibility");SetPropW(fresh,kWndProcLifetimeProperty,lifetime);refused=Json(Show(fresh));Check(!JBool(refused,"ok",true)&&showAllowed==1,"same-HWND cookie restoration never recovers quarantined binding");
 ResetSelection();{std::lock_guard<std::mutex> lock(g_presentWindowMx);g_presentWindows.clear();}const HWND inflight=CreateOwn();if(!inflight)return 111;Chain inflightChain(inflight);bool destroyedDuringPresent=false;const auto destroyResult=D9PresentFrame(nullptr,&inflightChain,nullptr,[&](){Check(Own(inflight)&&GetPropW(inflight,kPresentWindowLifetime)!=nullptr,"production Present reserves private lifetime before invoking the synthetic native provider");if(Own(inflight))destroyedDuringPresent=DestroyWindow(inflight)!=FALSE;return S_OK;});Check(destroyResult==S_OK&&destroyedDuringPresent&&!IsWindow(inflight)&&!HiddenPresentedWindow().selected.Unique()&&!g_hwnd,"destroyed in-flight native Present window cannot publish adoption evidence or be restamped");
 Check(showAfterFailed==0&&GuardIntact(),"terminal desktop writer guards and independent own lifetime/offscreen checks remain intact");
 for(auto it=owned.rbegin();it!=owned.rend();++it)if(IsWindow(it->window)){if(!Own(it->window)){Check(false,"owned cleanup lifetime");continue;}DestroyWindow(it->window);}g_hwnd=nullptr;CloseHandle(g_capEvent);CloseHandle(retained);Check(GuardIntact(),"terminal guards persist through all owned window cleanup");
 printf("HIDDEN_WINDOW_NATIVE_RESULT %u %u\n",passed,failed);return failed?1:0;
}
