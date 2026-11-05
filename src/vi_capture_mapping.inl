// Persist the last encoded/returned image geometry in the target, not a CLI process.
// The resize CAS can change only image dimensions, never client origin/coverage.
std::string CaptureMappingNumber(double value){char bytes[64];const auto result=std::to_chars(bytes,bytes+sizeof(bytes),value,std::chars_format::general,17);return result.ec==std::errc()?std::string(bytes,result.ptr):"0";}
void CaptureMappingFields(JOut& out,const SavedCaptureMap& map,unsigned long long serial){
 out.num("capture_map_version",1);out.str("capture_map_id",std::to_string(serial));JOut mapping;
 mapping.raw("scale",CaptureMappingNumber(map.cw>0?(double)map.w/map.cw:0));mapping.raw("scale_y",CaptureMappingNumber(map.ch>0?(double)map.h/map.ch:0));
 mapping.raw("offset_x",CaptureMappingNumber(map.ox));mapping.raw("offset_y",CaptureMappingNumber(map.oy));mapping.num("image_w",map.w);mapping.num("image_h",map.h);out.raw("capture_mapping",mapping.done());
}
bool CaptureMappingResize(SavedCaptureMap& map,unsigned long long& serial,const JObj& request){
 if(!map.valid||!serial||serial==(std::numeric_limits<unsigned long long>::max)()||!JHas(request,"capture_map_id")||request.at("capture_map_id").t!=1||
    JStr(request,"capture_map_id")!=std::to_string(serial)||!JHas(request,"expected_capture_scope")||request.at("expected_capture_scope").t!=1||
    !vi_capture_fence::Matches(JStr(request,"expected_capture_scope"),CaptureScopeJson(map.scope)))return false;
 if(!JHas(request,"image_w")||!JHas(request,"image_h")||request.at("image_w").t!=2||request.at("image_h").t!=2)return false;
 const double w=JNum(request,"image_w",0),h=JNum(request,"image_h",0);
 if(!std::isfinite(w)||!std::isfinite(h)||w<1||h<1||w>32768||h>32768||std::floor(w)!=w||std::floor(h)!=h||w*h>64*1024*1024)return false;
 map.w=(UINT)w;map.h=(UINT)h;++serial;return true;
}
std::string CaptureMappingCommand(const JObj& request,bool resize){
 std::lock_guard<std::recursive_mutex> input(g_inputMx);const char* reason=nullptr;
 if(!CaptureMapCurrent(&reason))return CaptureScopeFailure(reason);
 if(resize&&!CaptureMappingResize(g_savedCaptureMap,g_captureMapSerial,request)){JOut out;out.bol("ok",false);out.bol("input_injected",false);out.str("error","capture mapping resize did not match the current map/scope or bounded image dimensions; map was preserved");return out.done();}
 JOut out;out.bol("ok",true);out.num("pid",g_pid);out.bol("input_injected",false);out.bol("capture_space_updated",resize);
 CaptureMappingFields(out,g_savedCaptureMap,g_captureMapSerial);out.raw("capture_scope",CaptureScopeJson(g_savedCaptureMap.scope));return out.done();
}
