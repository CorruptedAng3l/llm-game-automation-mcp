// Coordinate maps belong to one adopted window lifetime and observed geometry.
// The private property is window-object storage, independent of human intent.
struct CaptureScope {
 HWND window=nullptr;DWORD thread=0;unsigned long long adoption=0,geometry=0;
 ULONG_PTR lifetime=0;LONG width=0,height=0;
};
std::atomic<ULONG_PTR> g_captureLifetimeSerial{0};
bool CaptureScopeTake(CaptureScope& result){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(g_jobRetargetDepth||!WndBindingInputAllowed(g_hwnd))return false;CaptureScope next;next.window=g_hwnd;next.adoption=g_inputReadyGeneration;next.geometry=g_captureGeometryEpoch;
 DWORD owner=0;next.thread=next.window?GetWindowThreadProcessId(next.window,&owner):0;RECT client{};
 if(!next.thread||owner!=g_pid||!GetClientRect(next.window,&client)||client.right<1||client.bottom<1)return false;
 next.width=client.right;next.height=client.bottom;next.lifetime=(ULONG_PTR)GetPropW(next.window,kCaptureLifetimeProperty);
 if(!next.lifetime){next.lifetime=++g_captureLifetimeSerial;if(!next.lifetime)next.lifetime=++g_captureLifetimeSerial;if(!SetPropW(next.window,kCaptureLifetimeProperty,(HANDLE)next.lifetime))return false;}
 RECT verified{};DWORD verifiedOwner=0;if(g_jobRetargetDepth||next.window!=g_hwnd||next.adoption!=g_inputReadyGeneration||next.geometry!=g_captureGeometryEpoch||GetWindowThreadProcessId(next.window,&verifiedOwner)!=next.thread||verifiedOwner!=g_pid||!GetClientRect(next.window,&verified)||verified.right!=next.width||verified.bottom!=next.height||(ULONG_PTR)GetPropW(next.window,kCaptureLifetimeProperty)!=next.lifetime)return false;
 result=next;return true;
}
bool CaptureScopeCurrent(const CaptureScope& old,const char** reason=nullptr){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);const char* why=nullptr;DWORD owner=0;RECT client{};
 if(g_jobRetargetDepth)why="adoption_in_progress";
 else if(!old.window||old.window!=g_hwnd||old.adoption!=g_inputReadyGeneration)why="adopted_window_changed";
 else if(!WndBindingInputAllowed(old.window))why="window_binding_unconfirmed";
 else if(!old.lifetime||GetWindowThreadProcessId(old.window,&owner)!=old.thread||owner!=g_pid||(ULONG_PTR)GetPropW(old.window,kCaptureLifetimeProperty)!=old.lifetime)why="window_lifetime_changed";
 else if(!GetClientRect(old.window,&client)||client.right<1||client.bottom<1)why="client_unavailable";
 else if(client.right!=old.width||client.bottom!=old.height)why="client_size_changed";
 else if(old.geometry!=g_captureGeometryEpoch)why="observed_geometry_changed";
 if(reason)*reason=why;return !why;
}
std::string CaptureScopeJson(const CaptureScope& scope){
 char hwnd[32];sprintf_s(hwnd,"0x%llX",(unsigned long long)(ULONG_PTR)scope.window);
 return "{\"hwnd\":\""+std::string(hwnd)+"\",\"thread\":"+std::to_string(scope.thread)+",\"adoption_generation\":\""+std::to_string(scope.adoption)+"\",\"geometry_epoch\":\""+std::to_string(scope.geometry)+"\",\"lifetime_cookie\":\""+std::to_string(scope.lifetime)+"\",\"client_w\":"+std::to_string(scope.width)+",\"client_h\":"+std::to_string(scope.height)+",\"geometry_witness\":\"client dimensions plus adopted WndProc WM_SIZE/WM_DPICHANGED epoch\"}";
}
