// A verified, bounded keyboard descriptor. Object instances are DIK scan codes;
// byte length alone never establishes an explicit custom layout.
#pragma once
#include <dinput.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
namespace vi_di_format {
// The SDK's linked c_dfDIKeyboard data uses this optional-object bit although
// current dinput.h does not publish a DIDFT_OPTIONAL macro.
constexpr DWORD kOptionalObjectBit=0x80000000u;
struct Binding{unsigned scan=0,offset=0;};
struct Plan{bool supported=false,standard=false;DWORD bytes=0;std::vector<Binding> keys;std::string reason;};
template<class Read>Plan Copy(LPCDIDATAFORMAT input,Read read){
    Plan out;DIDATAFORMAT format{};
    auto fail=[&](const char* reason){out.supported=false;out.reason=reason;return out;};
    if(!input||!read(input,&format,sizeof(format)))return fail("unreadable_format_header");
    out.bytes=format.dwDataSize;
    if(format.dwSize!=sizeof(DIDATAFORMAT)||format.dwObjSize!=sizeof(DIOBJECTDATAFORMAT))return fail("unsupported_descriptor_stride");
    if(!format.dwDataSize||format.dwDataSize>65536||!format.dwNumObjs||format.dwNumObjs>1024||!format.rgodf)return fail("descriptor_bound_or_empty");
    if(format.dwFlags&~(DIDF_ABSAXIS|DIDF_RELAXIS))return fail("unsupported_format_flags");
    const auto base=(std::uintptr_t)format.rgodf;
    if(base>(std::numeric_limits<std::uintptr_t>::max)()-((size_t)format.dwNumObjs*sizeof(DIOBJECTDATAFORMAT)))return fail("descriptor_address_overflow");
    out.keys.reserve(format.dwNumObjs);
    for(DWORD i=0;i<format.dwNumObjs;++i){
        DIOBJECTDATAFORMAT object{};GUID guid{};
        if(!read((const void*)(base+(size_t)i*sizeof(object)),&object,sizeof(object)))return fail("unreadable_object_descriptor");
        if(!object.pguid||!read(object.pguid,&guid,sizeof(guid)))return fail("unproved_key_guid");
        if(!IsEqualGUID(guid,GUID_Key))return fail("non_keyboard_object");
        const DWORD type=DIDFT_GETTYPE(object.dwType),scan=DIDFT_GETINSTANCE(object.dwType);
        if(object.dwType&~(kOptionalObjectBit|0x00ffffffu))return fail("unsupported_object_type_flags");
        if(!(type&DIDFT_BUTTON)||(type&~DIDFT_BUTTON)||scan>255)return fail("unproved_key_instance");
        if(object.dwOfs>=format.dwDataSize||object.dwFlags)return fail("unsupported_key_offset_or_flags");
        bool duplicate=false;for(const auto& key:out.keys){if(key.scan==scan&&key.offset!=object.dwOfs)return fail("ambiguous_key_instance");if(key.offset==object.dwOfs){if(key.scan!=scan)return fail("conflicting_key_offsets");duplicate=true;break;}}
        if(!duplicate)out.keys.push_back({scan,object.dwOfs});
    }
    out.supported=true;out.standard=out.bytes==256&&out.keys.size()==256;
    if(out.standard)for(const auto& key:out.keys)if(key.scan!=key.offset){out.standard=false;break;}
    return out;
}
inline bool MapsScan(const Plan& plan,unsigned scan){for(const auto& key:plan.keys)if(key.scan==scan)return true;return false;}
inline bool OffsetForScan(const Plan& plan,unsigned scan,DWORD& offset){for(const auto& key:plan.keys)if(key.scan==scan){offset=key.offset;return true;}return false;}
}
