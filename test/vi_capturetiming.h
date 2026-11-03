#pragma once
// Fixture-only UI-thread controls. Never used by product DLLs or desktop input.
namespace vi_capture_timing {
inline constexpr UINT kReset=WM_APP+117,kReport=WM_APP+118,kStall=WM_APP+119;
inline ULONG_PTR pendingReset=0,pendingReport=0;
inline bool Message(UINT message,WPARAM value) {
    if(message==kReset){pendingReset=value;return true;}
    if(message==kReport){pendingReport=value;return true;}
    if(message==kStall){Sleep((DWORD)(value>1000?1000:value));return true;}
    return false;
}
}
