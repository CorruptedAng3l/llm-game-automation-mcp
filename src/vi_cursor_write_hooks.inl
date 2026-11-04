BOOL WINAPI h_SetCursorPos(int x,int y){
 ++C.scur;std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 if(!CursorNativeAllowed())return CursorVirtualSet(vi_cursor_guard::SetPosition,x,y)?TRUE:FALSE;
 if(!o_SetCursorPos){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
 ++g_cursorCounts[vi_cursor_guard::SetPosition].forwarded;return o_SetCursorPos(x,y);
}
BOOL WINAPI h_SetPhysicalCursorPos(int x,int y){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 if(!CursorNativeAllowed())return CursorVirtualSet(vi_cursor_guard::SetPhysicalPosition,x,y)?TRUE:FALSE;
 if(!o_SetPhysicalCursorPos){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
 ++g_cursorCounts[vi_cursor_guard::SetPhysicalPosition].forwarded;return o_SetPhysicalCursorPos(x,y);
}
BOOL WINAPI h_ClipCursor(const RECT* requested){
 ++C.clip;auto& counters=g_cursorCounts[vi_cursor_guard::Clip];++counters.blocked;
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 if(!requested){g_virtualClipEnabled=false;++counters.virtualized;return TRUE;}
 RECT clip{};if(!CursorReadBytes(requested,&clip,sizeof(clip))){++counters.failedVirtual;SetLastError(ERROR_NOACCESS);return FALSE;}
 if(clip.right<clip.left||clip.bottom<clip.top){++counters.failedVirtual;SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 g_virtualClipRect=clip;g_virtualClipEnabled=true;++counters.virtualized;return TRUE;
}
BOOL WINAPI h_GetClipCursor(LPRECT output){
 if(!output){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 RECT clip=g_virtualClipRect;if(!g_virtualClipEnabled){clip.left=GetSystemMetrics(SM_XVIRTUALSCREEN);clip.top=GetSystemMetrics(SM_YVIRTUALSCREEN);clip.right=clip.left+GetSystemMetrics(SM_CXVIRTUALSCREEN);clip.bottom=clip.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);}
 if(!CursorBufferStore(output,&clip,sizeof(clip))){SetLastError(ERROR_NOACCESS);return FALSE;}return TRUE;
}
UINT WINAPI h_SendInput(UINT count,LPINPUT input,int size){
 if(!CursorNativeAllowed()){++g_cursorCounts[vi_cursor_guard::Send].blocked;SetLastError(ERROR_ACCESS_DENIED);return 0;}
 if(!o_SendInput){SetLastError(ERROR_PROC_NOT_FOUND);return 0;}++g_cursorCounts[vi_cursor_guard::Send].forwarded;return o_SendInput(count,input,size);
}
void WINAPI h_mouse_event(DWORD flags,DWORD x,DWORD y,DWORD data,ULONG_PTR extra){
 if(!CursorNativeAllowed()){++g_cursorCounts[vi_cursor_guard::MouseEvent].blocked;SetLastError(ERROR_ACCESS_DENIED);return;}
 if(!o_mouse_event){SetLastError(ERROR_PROC_NOT_FOUND);return;}++g_cursorCounts[vi_cursor_guard::MouseEvent].forwarded;o_mouse_event(flags,x,y,data,extra);
}
