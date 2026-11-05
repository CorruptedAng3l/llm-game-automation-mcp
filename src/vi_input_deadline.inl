#include "vi_input_deadline.h"
bool ReadInputDeadline(const JObj& request,std::uint64_t& deadline,std::string& error){
 deadline=0;if(!JHas(request,"admit_before_qpc_ticks")&&!JHas(request,"admit_qpc_frequency"))return true;
 if(OwnerCursorActive()){error="the user currently controls this target; no bounded helper input was admitted";return false;}
 std::uint64_t frequency=0;
 if(!JHas(request,"expected_capture_scope")||!JHas(request,"admit_before_qpc_ticks")||!JHas(request,"admit_qpc_frequency")||
  request.at("admit_before_qpc_ticks").t!=1||request.at("admit_qpc_frequency").t!=1||
  !vi_sequence_anchor::Parse(JStr(request,"admit_before_qpc_ticks"),deadline)||!vi_sequence_anchor::Parse(JStr(request,"admit_qpc_frequency"),frequency)||
  !vi_input_deadline::Admissible(deadline,frequency,vi_qpc::Now(),vi_qpc::Frequency())){deadline=0;error="input admission deadline is expired, unscoped, future beyond120s or on a different QPC clock; no input was injected";return false;}
 return true;
}
