#include "vi_native_clip.inl"
BOOL WINAPI h_SetCursorPos(int x,int y){
 ++C.scur; // Permission reads and original calls may reenter application code.
 // CursorVirtualSet retains its existing internal state lock.
 if(!CursorNativeAllowed())return CursorVirtualSet(vi_cursor_guard::SetPosition,x,y)?TRUE:FALSE;
 if(!o_SetCursorPos){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
 ClaimNativeOperation operation;if(!operation){++g_cursorCounts[vi_cursor_guard::SetPosition].blocked;SetLastError(operation.Reason()==vi_claim_native::LiveClaim?ERROR_ACCESS_DENIED:ERROR_BUSY);return FALSE;}
 ++g_cursorCounts[vi_cursor_guard::SetPosition].forwarded;return o_SetCursorPos(x,y);
}
BOOL WINAPI h_SetPhysicalCursorPos(int x,int y){
 // Match the unchanged native permission rule without an outer input lock.
 // CursorVirtualSet retains its existing internal state lock.
 if(!CursorNativeAllowed())return CursorVirtualSet(vi_cursor_guard::SetPhysicalPosition,x,y)?TRUE:FALSE;
 if(!o_SetPhysicalCursorPos){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
 ClaimNativeOperation operation;if(!operation){++g_cursorCounts[vi_cursor_guard::SetPhysicalPosition].blocked;SetLastError(operation.Reason()==vi_claim_native::LiveClaim?ERROR_ACCESS_DENIED:ERROR_BUSY);return FALSE;}
 ++g_cursorCounts[vi_cursor_guard::SetPhysicalPosition].forwarded;return o_SetPhysicalCursorPos(x,y);
}
BOOL WINAPI h_ClipCursor(const RECT* requested){++C.clip;return NativeClipRequest(requested);}
BOOL WINAPI h_GetClipCursor(LPRECT output){
 if(!output){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 RECT clip=g_virtualClipRect;if(!g_virtualClipEnabled){clip.left=GetSystemMetrics(SM_XVIRTUALSCREEN);clip.top=GetSystemMetrics(SM_YVIRTUALSCREEN);clip.right=clip.left+GetSystemMetrics(SM_CXVIRTUALSCREEN);clip.bottom=clip.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);}
 if(!CursorBufferStore(output,&clip,sizeof(clip))){SetLastError(ERROR_NOACCESS);return FALSE;}return TRUE;
}
UINT WINAPI h_SendInput(UINT count,LPINPUT input,int size){
 if(!CursorNativeAllowed()){++g_cursorCounts[vi_cursor_guard::Send].blocked;SetLastError(ERROR_ACCESS_DENIED);return 0;}
 if(!o_SendInput){SetLastError(ERROR_PROC_NOT_FOUND);return 0;}
 ClaimNativeOperation operation;if(!operation){++g_cursorCounts[vi_cursor_guard::Send].blocked;SetLastError(ERROR_ACCESS_DENIED);return 0;}
 ++g_cursorCounts[vi_cursor_guard::Send].forwarded;return o_SendInput(count,input,size);
}
void WINAPI h_mouse_event(DWORD flags,DWORD x,DWORD y,DWORD data,ULONG_PTR extra){
 if(!CursorNativeAllowed()){++g_cursorCounts[vi_cursor_guard::MouseEvent].blocked;SetLastError(ERROR_ACCESS_DENIED);return;}
 if(!o_mouse_event){SetLastError(ERROR_PROC_NOT_FOUND);return;}
 ClaimNativeOperation operation;if(!operation){++g_cursorCounts[vi_cursor_guard::MouseEvent].blocked;SetLastError(ERROR_ACCESS_DENIED);return;}
 ++g_cursorCounts[vi_cursor_guard::MouseEvent].forwarded;o_mouse_event(flags,x,y,data,extra);
}
