#pragma once
#include <windows.h>
#include <climits>
namespace vi_d9_fixture_geometry {
inline bool Outside(const RECT& r,const RECT& desktop){return r.right>r.left&&r.bottom>r.top&&desktop.right>desktop.left&&desktop.bottom>desktop.top&&(r.right<=desktop.left||r.left>=desktop.right||r.bottom<=desktop.top||r.top>=desktop.bottom);}
inline bool Predict(const RECT& before,const RECT& desktop,int x,int y,int width,int height,UINT flags,RECT& after){
 constexpr UINT allowed=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED;
 if((flags&~allowed)||!Outside(before,desktop))return false;
 const long long left=flags&SWP_NOMOVE?before.left:x,top=flags&SWP_NOMOVE?before.top:y;
 const long long w=flags&SWP_NOSIZE?(long long)before.right-before.left:width,h=flags&SWP_NOSIZE?(long long)before.bottom-before.top:height;
 if(w<1||h<1||w>8192||h>8192||left+w>LONG_MAX||top+h>LONG_MAX)return false;
 after={(LONG)left,(LONG)top,(LONG)(left+w),(LONG)(top+h)};return Outside(after,desktop);
}
}
