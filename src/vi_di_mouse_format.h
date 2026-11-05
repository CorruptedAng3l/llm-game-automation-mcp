// Explicit relative-mouse object identities and byte extents. Never infer a
// custom mouse layout from its state size or accept absolute axes as deltas.
#pragma once
#include "vi_di_keyboard_format.h"
namespace vi_di_mouse_format {
struct Binding {DWORD source=0,offset=0,width=0,instance=0;bool button=false;};
struct Plan {bool parsed=false,supported=false;DWORD bytes=0;std::vector<Binding> objects;std::string reason;};
template<class Read>Plan Copy(LPCDIDATAFORMAT input,Read read){
    Plan out;DIDATAFORMAT format{};
    auto fail=[&](const char* reason){out.supported=false;out.reason=reason;return out;};
    if(!input||!read(input,&format,sizeof(format)))return fail("unreadable_mouse_format");
    out.bytes=format.dwDataSize;
    if(format.dwSize!=sizeof(format)||format.dwObjSize!=sizeof(DIOBJECTDATAFORMAT))return fail("unsupported_mouse_descriptor_stride");
    if(format.dwFlags!=DIDF_RELAXIS)return fail("mouse_requires_relative_axes");
    if(!format.dwDataSize||format.dwDataSize>65536||!format.dwNumObjs||format.dwNumObjs>32||!format.rgodf)return fail("mouse_descriptor_bound_or_empty");
    const auto base=(std::uintptr_t)format.rgodf;
    if(base>(std::numeric_limits<std::uintptr_t>::max)()-(size_t)format.dwNumObjs*sizeof(DIOBJECTDATAFORMAT))return fail("mouse_descriptor_address_overflow");
    for(DWORD i=0;i<format.dwNumObjs;++i){
        DIOBJECTDATAFORMAT object{};GUID guid{};
        if(!read((const void*)(base+(size_t)i*sizeof(object)),&object,sizeof(object))||(object.pguid&&!read(object.pguid,&guid,sizeof(guid))))return fail("unproved_mouse_object_guid");
        if(object.dwFlags||(object.dwType&~(vi_di_format::kOptionalObjectBit|0x00ffffffu)))return fail("unsupported_mouse_object_flags");
        const DWORD type=DIDFT_GETTYPE(object.dwType),instance=DIDFT_GETINSTANCE(object.dwType);
        Binding binding{};
        if(IsEqualGUID(guid,GUID_XAxis)||IsEqualGUID(guid,GUID_YAxis)||IsEqualGUID(guid,GUID_ZAxis)){
            if(type!=DIDFT_RELAXIS&&type!=DIDFT_AXIS)return fail("unproved_relative_mouse_axis");
            binding.source=IsEqualGUID(guid,GUID_XAxis)?DIMOFS_X:IsEqualGUID(guid,GUID_YAxis)?DIMOFS_Y:DIMOFS_Z;binding.width=sizeof(LONG);
            const DWORD expected=binding.source/sizeof(LONG);
            if(instance!=DIDFT_GETINSTANCE(DIDFT_ANYINSTANCE)&&instance!=expected)return fail("unproved_mouse_axis_instance");
        }else if(!object.pguid||IsEqualGUID(guid,GUID_Button)){
            if(!(type&DIDFT_BUTTON)||(type&~DIDFT_BUTTON))return fail("unproved_mouse_button_type");
            binding.button=true;binding.instance=instance;binding.width=1;
        }else return fail("unsupported_mouse_object_guid");
        binding.offset=object.dwOfs;
        if(binding.offset>format.dwDataSize||binding.width>format.dwDataSize-binding.offset)return fail("mouse_object_outside_state");
        for(const auto& prior:out.objects){
            if(!prior.button&&!binding.button&&prior.source==binding.source)return fail("ambiguous_mouse_object_instance");
            if(binding.offset<prior.offset+prior.width&&prior.offset<binding.offset+binding.width)return fail("overlapping_mouse_object_storage");
        }
        out.objects.push_back(binding);
    }
    out.parsed=true;return out; // Native object metadata must resolve every mapping.
}
inline bool OffsetForSource(const Plan& plan,DWORD source,DWORD& offset){for(const auto& object:plan.objects)if(object.source==source){offset=object.offset;return true;}return false;}
}
