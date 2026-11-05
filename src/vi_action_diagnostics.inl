// Reply-time cumulative telemetry; it does not change routing or admission.
std::string ActionMouseDiagnostics(std::string response,const std::string& cmd){
 if(!CursorInputProducer(cmd)||g_acProduct.load()||response.empty()||response.back()!='}')return response;
 JOut counters;
#define VI_MOUSE_COUNT(name) counters.num(#name,(double)C.name.load())
 VI_MOUSE_COUNT(wm_mouse);VI_MOUSE_COUNT(wm_mouse_dropped);VI_MOUSE_COUNT(wm_mouse_owned);VI_MOUSE_COUNT(wm_mouse_owned_delivered);VI_MOUSE_COUNT(wm_mouse_owned_dropped);VI_MOUSE_COUNT(wm_mouse_unrecognized_dropped);
#undef VI_MOUSE_COUNT
 response.pop_back();response+=",\"mouse_message_health_version\":1,\"mouse_message_counters\":"+counters.done()+"}";return response;
}
