// Validate caller-observed scope before pointer preparation. Callers recheck it
// at each publication; no input mutex is held while waiting for hover/frames.
bool PointerScopeAdmit(const std::string& cmd,const JObj& j,CaptureScope& scope,std::string& error){
 const bool relative=JHas(j,"dx")||JHas(j,"dy"),absolute=JHas(j,"x")&&JHas(j,"y");
 auto integer=[&](const char* key,double fallback,double high){auto n=j.find(key);const double v=n==j.end()?fallback:n->second.n;return (n==j.end()||n->second.t==2)&&std::isfinite(v)&&v>=1&&v<=high&&v==std::floor(v);};
 const bool flags=(!JHas(j,"raw")||j.at("raw").t==3)&&(!JHas(j,"frame_paced")||j.at("frame_paced").t==3);
 const bool motion=(cmd=="mouse"||cmd=="move")&&flags&&
  ((cmd=="move"&&relative&&!JHas(j,"x")&&!JHas(j,"y")&&JBool(j,"raw",false)&&integer("steps",1,1)&&!JBool(j,"frame_paced",false))||
   (absolute&&!relative&&!JBool(j,"raw",false)&&integer("steps",1,200)));
 const bool click=absolute&&!relative&&((cmd=="click"&&integer("count",1,10))||(cmd=="mousedown"&&integer("count",1,1)));
 const bool release=cmd=="mouseup"&&!relative&&(absolute||(!JHas(j,"x")&&!JHas(j,"y")));
 const auto i=j.find("expected_capture_scope");
 if(i==j.end()||i->second.t!=1||(!motion&&!click&&!release)){error="scoped pointer input requires positioned click/count1..10, explicit down/up, absolute steps1..200, or one raw-relative step; no input was injected";return false;}
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 std::uint64_t deadline=0;if(!ReadInputDeadline(j,deadline,error))return false;
 if(OwnerCursorActive()){error="the user currently controls this target; no scoped pointer input was injected";return false;}
 if(!CaptureScopeTake(scope)||!vi_capture_fence::Matches(JStr(j,"expected_capture_scope"),CaptureScopeJson(scope))){error="pointer capture scope no longer matches target lifetime/geometry; no input was injected";return false;}
 return true;
}
