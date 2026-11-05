// Read-only eligibility for an already selected retained process. Enumeration is
// NOT presenting proof, injection authority, adoption, queue readiness or visibility.
// The caller must still require one fresh process in the proven launch tree.
#include "vi_present_window.h"
namespace vi_launch_hidden {
inline bool Number(const std::string& text,unsigned long long maximum,unsigned long long& value,bool zero=false){value=0;if(text.empty()||(!zero&&text[0]=='0')||(text.size()>1&&text[0]=='0'))return false;for(char c:text){if(c<'0'||c>'9'||(unsigned)(c-'0')>maximum||value>(maximum-(unsigned)(c-'0'))/10)return false;value=value*10+(unsigned)(c-'0');}return zero||value!=0;}
inline bool String(const vi_ready_json::Reader& reader,const char* key,std::string& value){auto field=reader.fields.find(key);if(field==reader.fields.end()||field->second.type!='"')return false;vi_ready_json::Reader decoded{field->second.raw,0,{}};value.clear();return decoded.String(value)&&decoded.at==field->second.raw.size();}
inline bool Uint(const vi_ready_json::Reader& reader,const char* key,unsigned long long maximum,unsigned long long& value,bool quoted=false,bool zero=false){auto field=reader.fields.find(key);if(field==reader.fields.end()||field->second.type!=(quoted?'"':'n'))return false;std::string text;if(quoted){if(!String(reader,key,text))return false;}else text=field->second.raw;return Number(text,maximum,value,zero);}
inline bool Handle(const std::string& text,HWND& window){window=nullptr;if(text.size()<3||text[0]!='0'||text[1]!='x'||text.size()>2+sizeof(ULONG_PTR)*2)return false;ULONG_PTR value=0;for(size_t i=2;i<text.size();++i){const int digit=vi_ready_json::Reader::Hex(text[i]);if(digit<0)return false;value=value*16+(unsigned)digit;}window=(HWND)value;return true;}
inline bool Process(HANDLE process,DWORD pid,unsigned long long created){
 if(!process||process==INVALID_HANDLE_VALUE||!pid||!created||WaitForSingleObject(process,0)!=WAIT_TIMEOUT||GetProcessId(process)!=pid)return false;
 FILETIME b{},e{},k{},u{};if(!GetProcessTimes(process,&b,&e,&k,&u))return false;ULARGE_INTEGER actual{};actual.LowPart=b.dwLowDateTime;actual.HighPart=b.dwHighDateTime;if(actual.QuadPart!=created)return false;
 wchar_t image[32768]{};DWORD size=32768;return QueryFullProcessImageNameW(process,0,image,&size)&&size&&size<32768;
}
inline bool Window(HWND window,DWORD pid,unsigned long long created,vi_present_window::Proof& p){
 p={};p.window=(ULONG_PTR)window;p.created=created;DWORD owner=0;p.thread=GetWindowThreadProcessId(window,&owner);p.pid=owner;if(!p.thread||owner!=pid||!IsWindow(window))return false;
 const HWND parent=GetParent(window);const HWND actualParent=GetAncestor(window,GA_PARENT),desktop=GetDesktopWindow();p.top=desktop&&GetAncestor(window,GA_ROOT)==window&&actualParent==desktop;p.child=(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD)!=0;p.messageOnly=parent==HWND_MESSAGE||!desktop||actualParent!=desktop;p.ownedPopup=GetWindow(window,GW_OWNER)!=nullptr;p.tool=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)!=0;p.visible=IsWindowVisible(window)!=FALSE;
 RECT client{};if(!GetClientRect(window,&client))return false;p.width=client.right;p.height=client.bottom;wchar_t name[128]{};if(!GetClassNameW(window,name,128))return false;p.dummy=!wcscmp(name,L"vinput_dummy_dxgi")||!wcscmp(name,L"vinput.d3d9.hidden.probe")||!wcscmp(name,L"#32770");p.cookie=(ULONG_PTR)GetPropW(window,L"vinput.presentation_window.lifetime.v1");DWORD again=0;return GetWindowThreadProcessId(window,&again)==p.thread&&again==pid;
}
struct Enumeration {bool process_matched=false,complete=false;unsigned scanned=0,hidden_count=0;HWND unique=nullptr;DWORD thread=0;std::string reason;};
struct EnumerationContext {DWORD pid;unsigned long long created;Enumeration result;bool capped=false,unconfirmed=false;};
inline BOOL CALLBACK Enumerate(HWND window,LPARAM parameter){auto& c=*(EnumerationContext*)parameter;if(++c.result.scanned>4096){c.capped=true;return FALSE;}DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=c.pid||IsWindowVisible(window))return TRUE;vi_present_window::Proof p;if(!Window(window,c.pid,c.created,p)){c.unconfirmed=true;return TRUE;}if(!vi_present_window::Eligible(p,c.pid,c.created)||p.visible)return TRUE;++c.result.hidden_count;c.result.unique=c.result.hidden_count==1?window:nullptr;c.result.thread=c.result.hidden_count==1?p.thread:0;return TRUE;}
inline Enumeration Observe(HANDLE process,DWORD pid,unsigned long long created){
 EnumerationContext c{pid,created,{}};if(!Process(process,pid,created)){c.result.reason="retained_process_identity_unconfirmed";return c.result;}c.result.process_matched=true;const bool enumerated=EnumWindows(Enumerate,(LPARAM)&c)!=FALSE;c.result.complete=enumerated&&!c.capped&&!c.unconfirmed&&Process(process,pid,created);if(!c.result.complete){c.result.unique=nullptr;c.result.thread=0;c.result.reason="hidden_window_snapshot_unconfirmed_or_capped";}else c.result.reason=c.result.hidden_count>1?"hidden_window_ambiguous":c.result.hidden_count==1?"hidden_window_unproven":"no_hidden_window";return c.result;
}
struct NativeState {bool valid=false,adopted=false,hidden_known=false,hidden=false,presentation_proven=false;HWND window=nullptr,candidate=nullptr;DWORD thread=0;std::string reason;};
inline NativeState Parse(const std::string& response,HANDLE process,DWORD pid,unsigned long long created){
 NativeState out;out.reason="native_hidden_status_unconfirmed";if(!Process(process,pid,created))return out;
 vi_ready_json::Reader status{response,0,{},128};bool ok=false;unsigned long long value=0;std::string handle;
 if(!status.Parse()||!status.Bool("ok",ok)||!ok||!Uint(status,"pid",MAXDWORD,value)||value!=pid||!Uint(status,"hidden_window_version",1,value)||value!=1||!status.Bool("window_adopted",out.adopted)||!String(status,"hwnd",handle)||!Handle(handle,out.window))return out;
 const auto hidden=status.fields.find("window_hidden"),proof=status.fields.find("presentation_window");if(hidden==status.fields.end()||proof==status.fields.end()||proof->second.type!='{')return out;
 if(hidden->second.type=='b'){out.hidden_known=status.Bool("window_hidden",out.hidden);}else if(hidden->second.type!='z')return out;
 const auto& proofText=proof->second.raw;vi_ready_json::Reader evidence{proofText,0,{}};bool ambiguous=false,capacity=false,truncated=false,unconfirmed=false;unsigned long long count=0;
 if(!evidence.Parse()||!Uint(evidence,"version",1,value)||value!=1||!Uint(evidence,"process_creation_filetime",(std::numeric_limits<unsigned long long>::max)(),value,true)||value!=created||!Uint(evidence,"hidden_candidate_count",128,count,false,true)||!evidence.Bool("ambiguous",ambiguous)||!evidence.Bool("capacity_unconfirmed",capacity)||!evidence.Bool("evidence_unconfirmed",unconfirmed)||!evidence.Bool("candidates_truncated",truncated))return out;
 if(out.adopted){vi_present_window::Proof window;if(!out.window||!out.hidden_known||!Window(out.window,pid,created,window)||!vi_present_window::Eligible(window,pid,created)||window.visible==out.hidden)return out;out.thread=window.thread;}
 out.valid=true;out.reason=out.adopted?"native_window_adopted":"native_window_not_adopted";
 if(count==1&&!ambiguous&&!capacity&&!truncated&&!unconfirmed){
  auto list=evidence.fields.find("hidden_candidates");if(list==evidence.fields.end()||list->second.type!='['){out.valid=false;return out;}
  const auto& array=list->second.raw;vi_ready_json::Reader cursor{array,0,{}};cursor.Space();if(cursor.at>=array.size()||array[cursor.at++]!='['){out.valid=false;return out;}cursor.Space();const size_t begin=cursor.at;char kind=0;if(!cursor.Any(0,kind)||kind!='{'){out.valid=false;return out;}const auto rowText=array.substr(begin,cursor.at-begin);cursor.Space();if(cursor.at>=array.size()||array[cursor.at++]!=']'){out.valid=false;return out;}cursor.Space();if(cursor.at!=array.size()){out.valid=false;return out;}
  vi_ready_json::Reader row{rowText,0,{}};std::string candidate,source;unsigned long long thread=0,cookie=0,age=0;
  if(!row.Parse()||!String(row,"hwnd",candidate)||!Handle(candidate,out.candidate)||!out.candidate||!Uint(row,"pid",MAXDWORD,value)||value!=pid||!Uint(row,"thread",MAXDWORD,thread)||!Uint(row,"window_lifetime_cookie",(std::numeric_limits<ULONG_PTR>::max)(),cookie,true)||!Uint(row,"process_creation_filetime",(std::numeric_limits<unsigned long long>::max)(),value,true)||value!=created||!Uint(row,"observed_age_ms",vi_present_window::FreshMs,age,false,true)||!String(row,"source",source)||(source!="d3d9_present"&&source!="dxgi_d3d11_present"&&source!="dxgi_d3d12_present")){out.valid=false;return out;}
  vi_present_window::Proof current;if(!Window(out.candidate,pid,created,current)||!vi_present_window::Eligible(current,pid,created)||current.visible||current.thread!=thread||current.cookie!=cookie||(out.adopted&&out.window!=out.candidate)){out.valid=false;return out;}out.presentation_proven=true;
 }
 if(!Process(process,pid,created)){out.valid=false;out.presentation_proven=false;out.reason="retained_process_changed_during_status_validation";}return out;
}
} // namespace vi_launch_hidden
