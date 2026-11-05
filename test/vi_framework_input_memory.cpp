// Actual framework probe/routing/timing expressions, with finite read-only fixtureWindow endpoints.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <cstdio>
#include <cwchar>
#include "vi_framework_input.h"
static DWORD g_pid=42,fixtureOwner=42,threadId=10;static HWND fixtureWindow=(HWND)(ULONG_PTR)1,g_hwnd=fixtureWindow;
static std::atomic<unsigned long long> g_inputReadyGeneration{1};static std::recursive_mutex g_inputMx;
static std::wstring className=L"Chrome_WidgetWin_1";static int mutation=0;static bool classFail=false;
struct WndProcBinding{bool live=true;};static auto fixtureBinding=std::make_shared<WndProcBinding>(),otherBinding=std::make_shared<WndProcBinding>();
static std::shared_ptr<WndProcBinding> WndBindingFind(HWND h){return h==fixtureWindow?fixtureBinding:nullptr;}
static bool WndBindingLive(const std::shared_ptr<WndProcBinding>& b){return b&&b->live;}
static DWORD WINAPI WindowThread(HWND h,LPDWORD pid){if(pid)*pid=h?fixtureOwner:0;return h?threadId:0;}
static int WINAPI WindowClass(HWND,LPWSTR out,int size){if(classFail)return 0;wcscpy_s(out,(size_t)size,className.c_str());
 if(mutation==1)fixtureOwner=99;if(mutation==2)threadId++;if(mutation==3)g_hwnd=(HWND)(ULONG_PTR)2;if(mutation==4)++g_inputReadyGeneration;if(mutation==5)fixtureBinding=otherBinding;
 return (int)className.size();}
#define GetWindowThreadProcessId WindowThread
#define GetClassNameW WindowClass
struct Config{int msgmode=0,rawmode=0;}g_cfg;
struct RawReg{bool kbd=true,mouse=true,kbdNoLegacy=true,mouseNoLegacy=true;};
#include "actual_framework_routes.inl"
#include "actual_framework_probe.inl"
using JObj=std::map<std::string,double>;
static bool JHas(const JObj& j,const char* k){return j.count(k)!=0;}
static double JNum(const JObj& j,const char* k,double value){auto found=j.find(k);return found==j.end()?value:found->second;}
static bool JBool(const JObj& j,const char* k,bool value){auto found=j.find(k);return found==j.end()?value:found->second!=0;}
#include "actual_framework_timing.inl"
// The real status profile block is extracted too; this finite output sink never
// inspects a process or performs GUI/input calls.
struct JOut {std::string value="{";
 void Field(const char* key,const std::string& text){if(value.size()>1)value+=',';value+='"';value+=key;value+="\":";value+=text;}
 void num(const char* key,double n){Field(key,std::to_string(n));}
 void bol(const char* key,bool v){Field(key,v?"true":"false");}
 void str(const char* key,const std::string& text){Field(key,'"'+text+'"');}
 void raw(const char* key,const std::string& text){Field(key,text);}
 std::string done()const{return value+'}';}
};
static std::string W2U(const wchar_t* text){std::string out;while(*text)out+=(char)*text++;return out;}
#include "actual_framework_status.inl"
static bool UnityProfile(){
#ifdef VI_FRAMEWORK_UNITY_TIMING
 return FrameworkProfileForCurrent().unity;
#else
 return false;
#endif
}
static unsigned passed=0,failed=0;
static void Check(bool good,const char* text){printf("%s %s\n",good?"PASS":"FAIL",text);good?++passed:++failed;}
static void Reset(){fixtureOwner=g_pid;threadId=10;g_hwnd=fixtureWindow;g_inputReadyGeneration=1;className=L"Chrome_WidgetWin_1";mutation=0;classFail=false;fixtureBinding=std::make_shared<WndProcBinding>();otherBinding=std::make_shared<WndProcBinding>();g_cfg={};}
static bool Legacy(bool kbd,const RawReg& rr,bool ui=true){
#ifdef VI_FRAMEWORK_POLICY
 return MsgsWanted(kbd,rr,ui);
#else
 (void)ui;return MsgsWanted(kbd,rr);
#endif
}
static bool Raw(bool kbd,const RawReg& rr,bool ui=true){
#ifdef VI_FRAMEWORK_POLICY
 return RawWanted(kbd,rr,ui);
#else
 (void)ui;return RawWanted(kbd,rr);
#endif
}
int main(){
 (void)&WndBindingFind;(void)&WndBindingLive;(void)&WindowThread;(void)&WindowClass;(void)&JBool;(void)&JHas;
 const RawReg registered;
 for(bool keyboard:{false,true}){
  Reset();Check(Legacy(keyboard,registered)&&!Raw(keyboard,registered),"known Chromium UI selects exactly legacy under auto despite NOLEGACY subscription");
  Reset();Check(!Legacy(keyboard,registered,false)&&Raw(keyboard,registered,false),"relative raw/mouse-look route retains original NOLEGACY choice");
  Reset();g_cfg.msgmode=1;Check(Legacy(keyboard,registered)&&Raw(keyboard,registered),"explicit msgs on preserves original caller-selected dual routing");
  Reset();g_cfg.msgmode=2;Check(!Legacy(keyboard,registered)&&Raw(keyboard,registered),"explicit msgs off preserves raw subscription and blocks legacy");
  Reset();g_cfg.rawmode=1;Check(Legacy(keyboard,registered)&&Raw(keyboard,registered),"explicit raw post remains authoritative");
  Reset();g_cfg.rawmode=2;Check(Legacy(keyboard,registered)&&Raw(keyboard,registered),"explicit raw buffer remains authoritative at policy boundary");
  Reset();g_cfg.rawmode=3;Check(Legacy(keyboard,registered)&&!Raw(keyboard,registered),"explicit raw off never adds a synthetic raw edge");
  Reset();className=L"OtherGame";Check(!Legacy(keyboard,registered)&&Raw(keyboard,registered),"ordinary NOLEGACY target retains original route");
 }
 Reset();Check(FrameworkProfileForCurrent().chromium,"fresh owned retained Chrome_WidgetWin_1 is recognized");
 Reset();className=L"Chrome_WidgetWin_0";Check(FrameworkProfileForCurrent().chromium,"exact Chrome_WidgetWin_0 is recognized");
 for(const auto name:{L"chrome_widgetwin_1",L"Chrome_WidgetWin_2",L"CEF",L"OtherGame"}){Reset();className=name;Check(!FrameworkProfileForCurrent().chromium,"other/custom CEF class is not an automatic Chromium profile");}
 for(int change=0;change<8;++change){Reset();if(change==0)g_hwnd=nullptr;else if(change==1)fixtureOwner=99;else if(change==2)fixtureBinding->live=false;else mutation=change-2;
  Check(!FrameworkProfileForCurrent().chromium,"missing/foreign/quarantined/changed HWND-thread-generation-fixtureBinding refuses framework override");}
 Reset();classFail=true;Check(!FrameworkProfileForCurrent().chromium,"failed class query cannot select a profile");
 Reset();const JObj omitted;Check(ActualHold("click",omitted)==400&&ActualHover(omitted)==350,"native omitted click timing selects current Chromium 400/350ms defaults");
 Reset();const JObj fallback{{"ms",140},{"settle_ms",80},{"framework_default_hold",1},{"framework_default_hover",1}};Check(ActualHold("click",fallback)==400&&ActualHover(fallback)==350,"MCP omitted-value flags select current native profile over fallback timing");
 Reset();const JObj explicitTiming{{"ms",2000},{"settle_ms",0}};Check(ActualHold("click",explicitTiming)==2000&&ActualHover(explicitTiming)==0,"explicit long hold and zero hover remain exact");
 Reset();className=L"OtherGame";Check(ActualHold("click",fallback)==140&&ActualHover(fallback)==80,"unknown framework retains MCP timing without automatic changes");
 Reset();fixtureBinding->live=false;Check(ActualHold("click",fallback)==140&&ActualHover(fallback)==80,"quarantined profile cannot change click timing");
 Reset();className=L"UnityWndClass";Check(UnityProfile()&&!FrameworkProfileForCurrent().chromium,"exact fresh owned UnityWndClass selects a separate timing-only profile");
 Check(ActualHold("click",omitted)==250&&ActualHover(omitted)==250,"native omitted Unity click timing is250ms hover and250ms hold");
 Check(ActualHold("click",fallback)==250&&ActualHover(fallback)==250,"MCP omitted-value flags select Unity250/250 over backward-compatible fallback");
 Check(ActualHold("click",explicitTiming)==2000&&ActualHover(explicitTiming)==0,"Unity explicit2000ms hold and zero hover remain exact");
 const JObj holdOnly{{"ms",75}},hoverOnly{{"settle_ms",10}};
 Check(ActualHold("click",holdOnly)==75&&ActualHover(holdOnly)==250&&ActualHold("click",hoverOnly)==250&&ActualHover(hoverOnly)==10,"Unity overrides only each independently omitted value");
 Check(ActualHold("mousedown",omitted)==0&&ActualHold("mouseup",omitted)==0,"Unity timing does not add a timed hold to explicit down/up commands");
 for(bool keyboard:{false,true}){Check(!Legacy(keyboard,registered)&&Raw(keyboard,registered),"Unity retains original registered NOLEGACY/raw route under auto");Check(!Legacy(keyboard,registered,false)&&Raw(keyboard,registered,false),"Unity relative raw/mouse-look route remains unchanged");g_cfg.msgmode=1;Check(Legacy(keyboard,registered)&&Raw(keyboard,registered),"Unity explicit legacy on retains its existing route");g_cfg.msgmode=2;Check(!Legacy(keyboard,registered)&&Raw(keyboard,registered),"Unity explicit legacy off retains its existing route");g_cfg={};}
 const auto unityStatus=ActualFrameworkStatus();Check(unityStatus.find("\"profile\":\"unity_ui\"")!=std::string::npos&&unityStatus.find("\"timing_only\":true")!=std::string::npos&&unityStatus.find("\"default_click_hold_ms\":250")!=std::string::npos&&unityStatus.find("\"default_click_hover_ms\":250")!=std::string::npos&&unityStatus.find("\"auto_legacy_ui\":false")!=std::string::npos&&unityStatus.find("\"auto_raw_ui_suppressed\":false")!=std::string::npos,"actual Unity status block reports timing250/250 without claiming automatic routing");
 for(const auto name:{L"unitywndclass",L"UnityWndClass2",L"Unity",L"OtherGame"}){Reset();className=name;Check(!UnityProfile()&&ActualHold("click",fallback)==140&&ActualHover(fallback)==80,"unknown/lookalike class retains original click timing");}
 for(int change=0;change<8;++change){Reset();className=L"UnityWndClass";if(change==0)g_hwnd=nullptr;else if(change==1)fixtureOwner=99;else if(change==2)fixtureBinding->live=false;else mutation=change-2;Check(!UnityProfile(),"missing/foreign/quarantined/changed Unity window-thread-generation-binding cannot select timing");}
 Reset();className=L"UnityWndClass";classFail=true;Check(!UnityProfile()&&ActualHold("click",fallback)==140,"failed Unity class query cannot change timing");
 Reset();const auto chromiumStatus=ActualFrameworkStatus();Check(chromiumStatus.find("\"profile\":\"chromium_ui\"")!=std::string::npos&&chromiumStatus.find("\"default_click_hold_ms\":400")!=std::string::npos&&chromiumStatus.find("\"default_click_hover_ms\":350")!=std::string::npos&&chromiumStatus.find("\"auto_legacy_ui\":true")!=std::string::npos,"actual Chromium status and routing remain400/350 and legacy UI");
 printf("framework input: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
