// Successful real target Presents publish only metadata. Adoption stays on the
// monitor and uses the unchanged protected WndProc binding/retarget path.
const wchar_t kPresentWindowLifetime[]=L"vinput.presentation_window.lifetime.v1";
std::mutex g_presentWindowMx;std::map<HWND,vi_present_window::Evidence> g_presentWindows;
ULONG_PTR g_presentWindowSerial=0;std::atomic<unsigned long long> g_presentWindowRejected{0},g_presentWindowCapacity{0};
std::atomic<ULONGLONG> g_presentWindowOverflowUntil{0};
std::atomic<ULONGLONG> g_presentWindowUnconfirmedUntil{0};
unsigned long long PresentProcessCreation(){FILETIME b{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u))return 0;ULARGE_INTEGER n{};n.LowPart=b.dwLowDateTime;n.HighPart=b.dwHighDateTime;return n.QuadPart;}
vi_present_window::Proof PresentWindowProof(HWND window){
 vi_present_window::Proof p;p.window=(ULONG_PTR)window;DWORD owner=0;p.thread=window?GetWindowThreadProcessId(window,&owner):0;p.pid=owner;
 if(!window||!p.thread||owner!=g_pid||!IsWindow(window))return p;
 p.created=PresentProcessCreation();const HWND parent=GetParent(window);const auto style=GetWindowLongPtrW(window,GWL_STYLE),extended=GetWindowLongPtrW(window,GWL_EXSTYLE);
 const HWND actualParent=GetAncestor(window,GA_PARENT),desktop=GetDesktopWindow();p.top=desktop&&GetAncestor(window,GA_ROOT)==window&&actualParent==desktop;p.child=(style&WS_CHILD)!=0;p.messageOnly=parent==HWND_MESSAGE||!desktop||actualParent!=desktop;p.ownedPopup=GetWindow(window,GW_OWNER)!=nullptr;p.tool=(extended&WS_EX_TOOLWINDOW)!=0;p.visible=IsWindowVisible(window)!=FALSE;
 RECT client{};if(GetClientRect(window,&client)){p.width=client.right;p.height=client.bottom;}
 wchar_t name[128]{};if(!GetClassNameW(window,name,128))p.dummy=true;else p.dummy=!wcscmp(name,L"vinput_dummy_dxgi")||!wcscmp(name,L"vinput.d3d9.hidden.probe")||!wcscmp(name,L"#32770");
 p.cookie=(ULONG_PTR)GetPropW(window,kPresentWindowLifetime);return p;
}
bool PresentedWindowCurrent(const vi_present_window::Evidence& evidence){std::lock_guard<std::mutex> lock(g_presentWindowMx);auto found=g_presentWindows.find((HWND)evidence.proof.window);if(found==g_presentWindows.end())return false;const auto current=PresentWindowProof((HWND)evidence.proof.window);if(!vi_present_window::Same(found->second.proof,current))found->second.quarantined=true;return vi_present_window::Same(found->second.proof,evidence.proof)&&vi_present_window::Fresh(found->second,current,g_pid,PresentProcessCreation(),GetTickCount64());}
// Reserve a private window-object witness BEFORE the native Present call. An
// api=0/observed=0 row is preparation only, never evidence of a successful frame.
vi_present_window::Proof BeginPresentedWindow(HWND window){
 vi_present_window::Proof empty;if(g_acProduct.load())return empty;
 auto p=PresentWindowProof(window);const auto created=PresentProcessCreation();if(!vi_present_window::Eligible(p,g_pid,created))return empty;
 std::lock_guard<std::mutex> lock(g_presentWindowMx);auto found=g_presentWindows.find(window);
 if(found!=g_presentWindows.end()){
  if(found->second.quarantined||!vi_present_window::Same(found->second.proof,p)){found->second.quarantined=true;++g_presentWindowRejected;return p;}
  return p;
 }
 if(g_presentWindows.size()>=128){++g_presentWindowCapacity;return p;}
 if(p.cookie||g_presentWindowSerial==(std::numeric_limits<ULONG_PTR>::max)()){++g_presentWindowRejected;return p;}
 const ULONG_PTR cookie=++g_presentWindowSerial;if(!SetPropW(window,kPresentWindowLifetime,(HANDLE)cookie)){++g_presentWindowRejected;return p;}
 p.cookie=cookie;const auto verified=PresentWindowProof(window);if(!vi_present_window::Same(p,verified)||!vi_present_window::Eligible(verified,g_pid,created)){++g_presentWindowRejected;return p;}
 g_presentWindows.emplace(window,vi_present_window::Evidence{verified,0,0,false});return verified;
}
void EndPresentedWindow(const vi_present_window::Proof& before,unsigned api,HRESULT result){
 if(result!=S_OK||!vi_present_window::Source(api)||g_acProduct.load()||!before.window)return;
 const auto now=GetTickCount64(),created=PresentProcessCreation();const HWND window=(HWND)before.window;auto current=PresentWindowProof(window);
 std::lock_guard<std::mutex> lock(g_presentWindowMx);auto found=g_presentWindows.find(window);
 if(!before.cookie||found==g_presentWindows.end()||found->second.quarantined||!vi_present_window::Same(before,found->second.proof)||!vi_present_window::Same(before,current)||!vi_present_window::Eligible(current,g_pid,created)){
  if(found!=g_presentWindows.end())found->second.quarantined=true;
  if(!before.visible||!current.visible)g_presentWindowUnconfirmedUntil=(std::max)(g_presentWindowUnconfirmedUntil.load(),now+vi_present_window::FreshMs);
  if(g_presentWindows.size()>=128&&!before.cookie&&!before.visible)g_presentWindowOverflowUntil=(std::max)(g_presentWindowOverflowUntil.load(),now+vi_present_window::FreshMs);
  ++g_presentWindowRejected;return;
 }
 found->second.observed=now;found->second.api=api;
 if(found->second.completed!=(std::numeric_limits<std::uint64_t>::max)())++found->second.completed;found->second.completedQpc=vi_qpc::Now();
}
std::uint64_t CompletedPresentAt(HWND window,std::uint64_t edgeQpc){
 if(!window)return 0;std::lock_guard<std::mutex> lock(g_presentWindowMx);auto found=g_presentWindows.find(window);
 if(found==g_presentWindows.end())return 0;const auto& e=found->second;
 return !e.quarantined&&e.proof.cookie==(ULONG_PTR)GetPropW(window,kPresentWindowLifetime)&&e.completedQpc&&e.completedQpc<=edgeQpc&&e.completed!=(std::numeric_limits<std::uint64_t>::max)()?e.completed:0;
}
struct PresentWindowSelection {vi_present_window::Selection selected;vi_present_window::Evidence evidence;std::vector<vi_present_window::Evidence> listed;};
PresentWindowSelection HiddenPresentedWindow(){
 PresentWindowSelection result;const auto now=GetTickCount64(),created=PresentProcessCreation();std::lock_guard<std::mutex> lock(g_presentWindowMx);result.selected.capacity=now<g_presentWindowOverflowUntil.load();
 for(auto& row:g_presentWindows){const auto current=PresentWindowProof(row.first);if(!vi_present_window::Same(row.second.proof,current)){row.second.quarantined=true;if(!current.visible&&vi_present_window::Eligible(current,g_pid,created)&&vi_present_window::Source(row.second.api)&&now>=row.second.observed&&now-row.second.observed<=vi_present_window::FreshMs)g_presentWindowUnconfirmedUntil=(std::max)(g_presentWindowUnconfirmedUntil.load(),row.second.observed+vi_present_window::FreshMs);continue;}const auto count=result.selected.count;result.selected.Add(row.second,current,g_pid,created,now);if(result.selected.count!=count){if(result.listed.size()<8)result.listed.push_back(row.second);if(result.selected.count==1)result.evidence=row.second;}}
 result.selected.unconfirmed=now<g_presentWindowUnconfirmedUntil.load();
 return result;
}
