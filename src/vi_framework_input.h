#pragma once
#include <cwchar>
namespace vi_framework_input {
inline bool Chromium(const wchar_t* name) {
    return name && (!std::wcscmp(name,L"Chrome_WidgetWin_1") || !std::wcscmp(name,L"Chrome_WidgetWin_0"));
}
inline bool Unity(const wchar_t* name) {return name&&!std::wcscmp(name,L"UnityWndClass");}
// Explicit modes retain their original meaning. The automatic framework route
// uses legacy input for UI only, without adding a second automatic raw event.
inline bool Legacy(int mode,bool registered,bool noLegacy,bool chromium,bool ui=true) {
    if(mode==1)return true;if(mode==2)return false;
    return (chromium&&ui)||!(registered&&noLegacy);
}
inline bool Raw(int mode,int messageMode,bool registered,bool chromium,bool ui=true) {
    if(mode==3)return false;if(mode==1||mode==2)return true;
    return chromium&&ui&&messageMode==0?false:registered;
}
// Unity recognition changes omitted click timing only, never input routing.
inline double Hold(double requested,bool defaultRequested,bool chromium,bool unity=false) {return defaultRequested?(chromium?400:unity?250:requested):requested;}
inline double Hover(double requested,bool defaultRequested,bool chromium,bool unity=false) {return defaultRequested?(chromium?350:unity?250:requested):requested;}
}
