// Exact current CLI helper + immutable legacy8 helper; no Windows APIs.
#include <cctype>
#include <cstdio>
#include <string>
using DWORD=unsigned long;
#include "vi_ready_json.h"
std::string modeledWire;bool available=true;
static bool ReadinessTransport(DWORD,const std::string&,DWORD,std::string& out){out=modeledWire;return available;}
#include "vi_cli_cursor_guard.inl"
#include "legacy_guard.inl"
unsigned passed=0,failed=0;
void Check(bool value,const char* name){std::printf("%s %s\n",value?"PASS":"FAIL",name);value?++passed:++failed;}
std::string Failure(){
#if HAS_KEYBD
 return CursorGuardFailureText();
#else
 return {};
#endif
}
bool Capability(const std::string& value){
#if HAS_KEYBD
 return CursorKeybdCapabilityReady(value);
#else
 (void)value;return false;
#endif
}
int main(){
 const std::string old="{\"ok\":true,\"hook_ready\":true,\"cursor_hooks_ready\":true,\"cursor_guard_version\":1}";
 const std::string good=old.substr(0,old.size()-1)+",\"keybd_event_guard_version\":1,\"keybd_event_hooks_ready\":true}";
 auto changed=[&](const char* a,const char* b){auto text=good;const auto at=text.find(a);text.replace(at,std::string(a).size(),b);return text;};
 Check(LegacyCursorGuardHandshakeReady(good),"old CLI accepts successful new DLL legacy8 readiness contract");
 const auto queueNotReady=good.substr(0,good.find("true"))+"false"+good.substr(good.find("true")+4);
 Check(Capability(queueNotReady)&&!CursorGuardHandshakeReady(queueNotReady),"keybd capability remains distinct from full window/queue readiness failure");
 Check(CursorGuardHandshakeReady(good),"new CLI accepts typed additive keybd readiness");
 Check(!CursorGuardHandshakeReady(old),"new CLI refuses old DLL without keybd capability");
 Check(!CursorGuardHandshakeReady(changed("keybd_event_hooks_ready\":true","keybd_event_hooks_ready\":false")),"unarmed keybd marker refuses");
 Check(!CursorGuardHandshakeReady(changed("keybd_event_hooks_ready\":true","keybd_event_hooks_ready\":\"true\"")),"string keybd readiness is not proof");
 Check(!CursorGuardHandshakeReady(changed("keybd_event_guard_version\":1","keybd_event_guard_version\":\"1\"")),"string keybd version is not proof");
 Check(!CursorGuardHandshakeReady(changed("keybd_event_guard_version\":1","keybd_event_guard_version\":2")),"unknown keybd version refuses");
 Check(!CursorGuardHandshakeReady(changed("hook_ready\":true","hook_ready\":false")),"other mandatory hook failure still refuses");
 Check(!CursorGuardHandshakeReady(old.substr(0,old.size()-1)+",\"nested\":{\"keybd_event_guard_version\":1,\"keybd_event_hooks_ready\":true}}"),"nested keybd fields cannot impersonate capability");
 Check(!CursorGuardHandshakeReady(good.substr(0,good.size()-1)+",\"keybd_event_hooks_ready\":false}"),"duplicate readiness refuses");
 modeledWire=old;Check(!RequireCursorGuard(0)&&Failure().find("keybd_event guard capability")!=std::string::npos,"old DLL refusal explains missing required keybd capability before input");
 modeledWire=changed("keybd_event_hooks_ready\":true","keybd_event_hooks_ready\":false");Check(!RequireCursorGuard(0)&&Failure().find("keybd_event guard is unarmed")!=std::string::npos,"new unarmed DLL refusal explains keybd coverage failure");
 modeledWire=good;Check(RequireCursorGuard(0),"matching responder supports guarded input handshake");
 available=false;Check(!RequireCursorGuard(0),"unavailable observation never implies coverage");
 Check(NeedsCursorGuard("{\"cmd\":\"down\",\"vk\":124}")&&!NeedsCursorGuard("{\"cmd\":\"release\"}")&&CursorGuardSpecialCommand("drag")&&!CursorGuardSpecialCommand("status"),"producer and release classifications stay unchanged");
 std::printf("RESULT %u PASS %u FAIL\n",passed,failed);return failed?1:0;
}
