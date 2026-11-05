// keybd_event is global input, not a target-local virtual input producer.
using PFN_KeybdEvent=void(WINAPI*)(BYTE,BYTE,DWORD,ULONG_PTR);
PFN_KeybdEvent o_keybd_event=nullptr;
thread_local bool g_keybdPermissionProbe=false;
void WINAPI h_keybd_event(BYTE key,BYTE scan,DWORD flags,ULONG_PTR extra){
 const DWORD incoming=GetLastError();
 // Declared before the reservation: restore AFTER its destructor/telemetry.
 struct LastErrorScope{DWORD value;~LastErrorScope(){SetLastError(value);}} error{incoming};
 if(g_keybdPermissionProbe){++g_cursorCounts[vi_cursor_guard::KeybdEvent].blocked;error.value=ERROR_BUSY;return;}
 bool allowed=false;{struct ProbeScope{ProbeScope(){g_keybdPermissionProbe=true;}~ProbeScope(){g_keybdPermissionProbe=false;}} probe;allowed=CursorNativeAllowed();}
 if(!allowed){++g_cursorCounts[vi_cursor_guard::KeybdEvent].blocked;error.value=ERROR_ACCESS_DENIED;return;}
 if(!o_keybd_event){++g_cursorCounts[vi_cursor_guard::KeybdEvent].blocked;error.value=ERROR_PROC_NOT_FOUND;return;}
 ClaimNativeOperation operation;
 if(!operation){++g_cursorCounts[vi_cursor_guard::KeybdEvent].blocked;error.value=operation.Reason()==vi_claim_native::LiveClaim?ERROR_ACCESS_DENIED:ERROR_BUSY;return;}
 ++g_cursorCounts[vi_cursor_guard::KeybdEvent].forwarded;
 SetLastError(incoming);o_keybd_event(key,scan,flags,extra);error.value=GetLastError();
}
