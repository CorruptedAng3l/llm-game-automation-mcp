#pragma once
#include <string>
namespace vi_launch_exposure {
inline std::string Fields(unsigned selected,unsigned guarded,bool watching,bool observed){
 return ",\"pre_resume_guard_pid\":"+(guarded?std::to_string(guarded):"null")+
  ",\"selected_pid_hooks_loaded_before_resume\":"+(guarded&&selected==guarded?"true":"false")+
  ",\"startup_foreground_observed\":"+(watching?observed?"true":"false":"null")+
  ",\"startup_foreground_prevention_guaranteed\":false,\"startup_guard_scope\":\"foreground observation/hand-back covers identified launch targets while watched; early injection records only the exact suspended PID before resume; Steam/followed children and pre-hook activation are not covered by that pre-resume receipt; no absence-of-foreground-exposure claim\"";
}
}
