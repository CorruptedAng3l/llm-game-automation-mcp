struct SavedCaptureMap {bool valid=false;UINT w=0,h=0;double ox=0,oy=0,cw=0,ch=0;CaptureScope scope;};
SavedCaptureMap g_savedCaptureMap; // command-owned; watchdog/WndProc never write this snapshot
bool CaptureMapCurrent(const char** reason=nullptr){
 if(!g_savedCaptureMap.valid||!g_savedCaptureMap.w||!g_savedCaptureMap.h){if(reason)*reason="saved_capture_missing";return false;}
 return CaptureScopeCurrent(g_savedCaptureMap.scope,reason);
}
