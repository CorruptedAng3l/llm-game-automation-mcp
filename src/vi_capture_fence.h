#pragma once
#include <string>
namespace vi_capture_fence {
// The current token is produced from a freshly validated CaptureScope, not from
// caller JSON. Exact opaque comparison also refuses omitted/reordered fields.
inline bool Matches(const std::string& requested,const std::string& current){
    return requested.size()>=2&&requested.size()<=2048&&requested.front()=='{'&&requested.back()=='}'&&
        requested.find('\0')==std::string::npos&&requested==current;
}
}
