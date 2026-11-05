// Counts are observations on reached hooks, never device selection or consumption.
std::string PadApiCallsJson(unsigned begin,unsigned end,PadApiCalls (*calls)[5],bool installed){
 JOut out;unsigned long long total=0;for(unsigned slot=begin;slot<end;slot++)for(unsigned user=0;user<5;user++)total+=calls[slot][user].entries.load();
 const bool measured=installed||total!=0;out.str("coverage",measured?"partial_installed_or_observed_entries":"unmeasured");
 std::string users="[";
 for(unsigned user=0;user<5;user++){
  unsigned long long entries=0,ok=0,error=0;
  for(unsigned slot=begin;slot<end;slot++){entries+=calls[slot][user].entries.load();ok+=calls[slot][user].ok.load();error+=calls[slot][user].error.load();}
  JOut item;if(user<4)item.num("user",user);else item.str("user","other");
  if(measured){item.str("entries",std::to_string(entries));item.str("success_returns",std::to_string(ok));item.str("error_returns",std::to_string(error));}
  else{item.raw("entries","null");item.raw("success_returns","null");item.raw("error_returns","null");}
  if(user)users+=',';users+=item.done();
 }
 out.raw("users",users+"]");return out.done();
}
std::string PadApiCensusJson(){
 JOut out;out.num("version",1);out.str("scope","hook observations since this DLL instance; no exclusive backend/application-consumption inference; pre-injection and bypassed objects are unmeasured; counter snapshot is not transactional");
 const unsigned mask=g_padApiStateInstalled.load();out.num("xinput_state_install_mask",mask);
 out.raw("xinput_standard",PadApiCallsJson(0,4,g_padApiState,(mask&15)!=0));out.raw("xinput_extended",PadApiCallsJson(4,8,g_padApiState,(mask&240)!=0));
 bool capsInstalled=false;std::string exports="[";
 for(unsigned slot=0;slot<5;slot++){
  const unsigned state=g_padApiExportState[slot].load();JOut item;item.num("slot",slot);
  item.str("api",slot<4?"XInputGetCapabilities":"joyGetPosEx");if(slot<4)item.str("dll",slot==0?"xinput1_4.dll":slot==1?"xinput1_3.dll":slot==2?"xinput9_1_0.dll":"xinputuap.dll");else item.str("dll","winmm.dll");
  item.str("install",state==2?"installed_observed":state==4?"exact_typed_alias":state==3?"failed_or_unknown":state==1?"pending":"unmeasured_not_observed_loaded");
  if(state==4)item.num("canonical_slot",g_padApiExportAlias[slot].load());if(slot<4&&(state==2||state==4))capsInstalled=true;
  if(slot)exports+=',';exports+=item.done();
 }
 out.raw("exports",exports+"]");out.raw("xinput_capabilities",PadApiCallsJson(0,4,g_padApiCapabilities,capsInstalled));
 JOut joy;const bool joyMeasured=g_padApiExportState[4]==2||g_padApiJoy.entries!=0;
 joy.str("coverage",joyMeasured?"installed_or_observed":"unmeasured");
 if(joyMeasured){joy.str("entries",std::to_string(g_padApiJoy.entries.load()));joy.str("success_returns",std::to_string(g_padApiJoy.ok.load()));joy.str("error_returns",std::to_string(g_padApiJoy.error.load()));}else{joy.raw("entries","null");joy.raw("success_returns","null");joy.raw("error_returns","null");}out.raw("joy_get_pos_ex",joy.done());
 JOut di;
 for(unsigned kind=0;kind<4;kind++){
  const auto state=g_padApiDiState[kind].load(),buffer=g_padApiDiBuffer[kind].load();JOut entry;
  entry.str("coverage",state||buffer?"partial_observed_on_intercepted_implementation":"unmeasured");
  if(state)entry.str("state_entries",std::to_string(state));else entry.raw("state_entries","null");if(buffer)entry.str("buffer_entries",std::to_string(buffer));else entry.raw("buffer_entries","null");
  di.raw(kind==0?"joystick":kind==1?"gamepad":kind==2?"other_non_keyboard_mouse":"unknown_caps",entry.done());
 }
 di.str("scope","fresh current-call caps type at state/buffer entry, not a returned state or controller consumption; non-intercepted vtables/pre-injection calls remain unmeasured");out.raw("directinput",di.done());
 JOut raw;const bool registered=g_padApiRawRegistrationInstalled.load();const bool dataHook=g_padApiRawDataInstalled.load();raw.bol("registration_hook_installed_observed",registered);raw.bol("data_hook_installed_observed",dataHook);
 for(unsigned kind=0;kind<2;kind++){
  JOut entry;const auto reg=g_padApiHidRegistration[kind].registered.load(),removed=g_padApiHidRegistration[kind].removed.load(),excluded=g_padApiHidRegistration[kind].excluded.load();
  if(registered||reg||removed||excluded){entry.str("successful_registration_entries",std::to_string(reg));entry.str("successful_removal_entries",std::to_string(removed));entry.str("successful_exclusion_entries",std::to_string(excluded));}
  else{entry.raw("successful_registration_entries","null");entry.raw("successful_removal_entries","null");entry.raw("successful_exclusion_entries","null");}
  raw.raw(kind?"page1_usage5_gamepad":"page1_usage4_joystick",entry.done());
 }
 const auto hidInput=g_padApiHidInputReturns.load(),hidHeader=g_padApiHidHeaderReturns.load();
 if(dataHook||hidInput||hidHeader){raw.str("permitted_hid_input_header_returns",std::to_string(hidInput));raw.str("permitted_hid_header_query_returns",std::to_string(hidHeader));}else{raw.raw("permitted_hid_input_header_returns","null");raw.raw("permitted_hid_header_query_returns","null");}if(dataHook||g_padApiHidHeaderUnknown.load())raw.str("failed_header_observations",std::to_string(g_padApiHidHeaderUnknown.load()));else raw.raw("failed_header_observations","null");raw.raw("hid_buffer_reads","null");
 raw.str("scope","registration entries differ from data returns; HID type alone is not gamepad usage or physical provenance; denied/query-only/pre-injection payloads and buffered HID classification are unmeasured; no extra original read performed");out.raw("raw_hid",raw.done());
 out.raw("wgi_gamepad_current_reading","{\"coverage\":\"unmeasured\",\"calls\":null,\"reason\":\"no typed app-created object interception; never instantiated a controller to probe\"}");
 out.raw("gameinput_current_reading","{\"coverage\":\"unmeasured\",\"calls\":null,\"reason\":\"versioned header and app-created interface interception not installed\"}");return out.done();
}
