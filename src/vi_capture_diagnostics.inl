// Metadata only. Never selects/retargets a source or probes candidate pixels.
// Included before vi_overlaycapture.h; all state is command-owned under Exec's mutex.
const wchar_t kCaptureDiagLifetimeProperty[]=L"vinput.capture_diagnostics.window_lifetime.v1";
ULONG_PTR g_captureDiagLifetimeSerial=0;
struct CaptureDiagWindow {
 HWND window=nullptr,owner=nullptr;DWORD pid=0,thread=0;ULONG_PTR lifetime=0;
 LONG width=0,height=0;bool valid=false,visible=false,iconic=false,titleLimit=false,titleAvailable=false,unicode=true;
 std::wstring klass,title;
};
struct CaptureDiagnostic {
 bool available=false,enumerationTruncated=false,retainedTruncated=false,pixelsObserved=false,uniform=false,black=false;
 unsigned visited=0,candidatesObserved=0;unsigned long long adoption=0;
 CaptureDiagWindow input,source;std::vector<CaptureDiagWindow> candidates;
 std::string enumerationStop="end",transform="input_client_printwindow";
 int sourceX=0,sourceY=0,sourceW=0,sourceH=0,outputW=0,outputH=0;
};
CaptureDiagnostic g_captureDiagnostic;
bool CaptureDiagSameProcess(HWND window,DWORD& thread){DWORD pid=0;thread=GetWindowThreadProcessId(window,&pid);return thread&&pid==GetCurrentProcessId();}
CaptureDiagWindow CaptureDiagDescribe(HWND window){
 CaptureDiagWindow item;DWORD thread=0;if(!window||!CaptureDiagSameProcess(window,thread))return item;
 item.window=window;item.thread=thread;item.pid=GetCurrentProcessId();
 // Establish the witness BEFORE reading metadata. A recycled HWND must not
 // receive a new cookie after old-window metadata was already collected.
 item.lifetime=(ULONG_PTR)GetPropW(window,kCaptureDiagLifetimeProperty);
 if(!item.lifetime){DWORD beforeSet=0;if(!CaptureDiagSameProcess(window,beforeSet)||beforeSet!=thread)return CaptureDiagWindow{};
  item.lifetime=++g_captureDiagLifetimeSerial;if(!item.lifetime)item.lifetime=++g_captureDiagLifetimeSerial;
  if(!SetPropW(window,kCaptureDiagLifetimeProperty,(HANDLE)item.lifetime))return CaptureDiagWindow{};}
 DWORD stampedThread=0;if(!CaptureDiagSameProcess(window,stampedThread)||stampedThread!=thread||
  (ULONG_PTR)GetPropW(window,kCaptureDiagLifetimeProperty)!=item.lifetime)return CaptureDiagWindow{};
 RECT r{};if(!GetClientRect(window,&r))return CaptureDiagWindow{};item.width=r.right;item.height=r.bottom;
 wchar_t name[128]{},title[256]{};if(!GetClassNameW(window,name,128))return CaptureDiagWindow{};item.klass=name;
 // Default caption storage only: unlike GetWindowTextW on our own windows,
 // this does not dispatch custom WM_GETTEXT code on an application thread.
 // Custom-drawn/dynamic application text is explicitly outside this title field.
 item.unicode=IsWindowUnicode(window)!=FALSE;
 if(item.unicode){const LRESULT copied=DefWindowProcW(window,WM_GETTEXT,256,(LPARAM)title);
  if(copied>=0&&copied<256){title[255]=0;item.title.assign(title,(size_t)copied);item.titleLimit=copied==255;item.titleAvailable=true;}}
 else{char narrow[256]{};const LRESULT copied=DefWindowProcA(window,WM_GETTEXT,256,(LPARAM)narrow);
  if(copied>=0&&copied<256){narrow[255]=0;item.titleLimit=copied==255;
   if(!copied)item.titleAvailable=true;
   else{const int converted=MultiByteToWideChar(CP_ACP,0,narrow,(int)copied,title,256);if(converted>0){item.title.assign(title,(size_t)converted);item.titleAvailable=true;}}}}
 item.visible=IsWindowVisible(window)!=FALSE;item.iconic=IsIconic(window)!=FALSE;item.owner=GetWindow(window,GW_OWNER);
 DWORD afterThread=0;RECT afterClient{};item.valid=CaptureDiagSameProcess(window,afterThread)&&afterThread==item.thread&&
  (ULONG_PTR)GetPropW(window,kCaptureDiagLifetimeProperty)==item.lifetime&&GetClientRect(window,&afterClient)&&
  afterClient.right==item.width&&afterClient.bottom==item.height;
 return item.valid?item:CaptureDiagWindow{};
}
bool CaptureDiagCurrent(const CaptureDiagWindow& item){
 DWORD thread=0;RECT r{};return item.valid&&item.lifetime&&CaptureDiagSameProcess(item.window,thread)&&thread==item.thread&&
  (ULONG_PTR)GetPropW(item.window,kCaptureDiagLifetimeProperty)==item.lifetime&&GetClientRect(item.window,&r)&&r.right==item.width&&r.bottom==item.height;
}
bool CaptureDiagAdoptionCurrent(const CaptureDiagnostic& diagnostic){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 return g_jobRetargetDepth.load()==0&&g_wndConfigCommitDepth.load()==0&&g_hwnd==diagnostic.input.window&&g_inputReadyGeneration.load()==diagnostic.adoption&&CaptureDiagCurrent(diagnostic.input);
}
void CaptureDiagReset(){g_captureDiagnostic=CaptureDiagnostic{};}
void CaptureDiagBegin(HWND input,int width,int height){
 CaptureDiagReset();auto& d=g_captureDiagnostic;d.available=true;d.adoption=g_inputReadyGeneration.load();
 d.input=CaptureDiagDescribe(input);d.source=d.input;d.sourceW=width;d.sourceH=height;d.outputW=width;d.outputH=height;
 // Enumeration is bounded by visited and retained counts plus a best-effort
 // elapsed budget. It invokes no candidate WndProc and performs no PrintWindow.
 const ULONGLONG start=GetTickCount64();HWND next=GetTopWindow(nullptr);
 while(next){
  if(d.visited>=4096){d.enumerationTruncated=true;d.enumerationStop="visit_limit";break;}
  if(GetTickCount64()-start>=12){d.enumerationTruncated=true;d.enumerationStop="elapsed_budget";break;}
  const HWND window=next;next=GetWindow(window,GW_HWNDNEXT);++d.visited;
  if(next==window){d.enumerationTruncated=true;d.enumerationStop="enumeration_cycle";break;}
  DWORD thread=0;RECT client{};
  if(window==input||!CaptureDiagSameProcess(window,thread)||!IsWindowVisible(window)||!GetClientRect(window,&client)||client.right<1||client.bottom<1)continue;
  ++d.candidatesObserved;if(d.candidates.size()>=16){d.retainedTruncated=true;continue;}
  auto candidate=CaptureDiagDescribe(window);if(candidate.valid)d.candidates.push_back(std::move(candidate));
 }
}
void CaptureDiagSource(const CaptureDiagWindow& source,int x,int y,int width,int height){
 auto& d=g_captureDiagnostic;d.source=source;d.sourceX=x;d.sourceY=y;d.sourceW=width;d.sourceH=height;
 d.transform="existing_presentation_center_crop_to_input_client";
}
void CaptureDiagPixels(const std::vector<BYTE>& pixels){
 auto& d=g_captureDiagnostic;const auto expected=(size_t)d.outputW*(size_t)d.outputH*4;
 if(d.outputW<1||d.outputH<1||pixels.size()!=expected||pixels.empty())return;
 d.pixelsObserved=true;d.uniform=true;d.black=true;
 for(size_t n=0;n<pixels.size();n+=4){if(pixels[n]||pixels[n+1]||pixels[n+2])d.black=false;
  if(pixels[n]!=pixels[0]||pixels[n+1]!=pixels[1]||pixels[n+2]!=pixels[2])d.uniform=false;
  if(!d.uniform&&!d.black)break;}
}
