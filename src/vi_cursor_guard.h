#pragma once
#include <atomic>
#include <cstdint>
namespace vi_cursor_guard {
enum Api {SetPosition,SetPhysicalPosition,Clip,Send,MouseEvent,GetClip,GetPosition,GetPhysicalPosition,ApiCount};
inline const char* Name(unsigned api){static const char* names[]={"SetCursorPos","SetPhysicalCursorPos","ClipCursor","SendInput","mouse_event","GetClipCursor","GetCursorPos","GetPhysicalCursorPos"};return api<ApiCount?names[api]:"unknown";}
struct Arbitration {std::uint64_t takeover=0,automation=0;void Human(std::uint64_t token){if(token>takeover)takeover=token;}bool AdmitAgent(std::uint64_t token,bool accepted){if(!accepted||!token||token<=automation)return false;automation=token;return true;}bool Holding()const{return takeover>automation;}bool Automation()const{return automation&&automation>=takeover;}};
struct Owner {bool foregroundVerified=false,foregroundOwned=false,windowReady=false,humanFresh=false,lease=false,forcedVirtual=false,humanHolding=false,agentPosition=false;std::uint64_t humanEpoch=0,agentEpoch=0;};
inline bool ReaderAllowed(const Owner& owner){return owner.foregroundVerified&&owner.foregroundOwned;}
inline bool NativeWriteAllowed(const Owner& owner){return ReaderAllowed(owner)&&owner.windowReady&&(owner.humanFresh||(owner.lease&&!owner.forcedVirtual));}
inline bool VirtualPositionReads(const Owner& owner){return !ReaderAllowed(owner)||(!owner.humanHolding&&(owner.forcedVirtual||owner.agentPosition));}
inline bool NativeClipAllowed(const Owner&){return false;}
inline const char* WriteReason(const Owner& owner){return !owner.foregroundVerified?"foreground_unverified":!owner.foregroundOwned?"background":!owner.windowReady?"window_not_adopted":owner.forcedVirtual&&!owner.humanFresh?"forced_virtual":!(owner.humanFresh||owner.lease)?"owner_intent_or_lease_missing":"owner_active";}
struct Counts {std::atomic<unsigned long long> blocked{0},virtualized{0},forwarded{0},unownedForwarded{0},failedVirtual{0};};
}
