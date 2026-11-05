// Escape decision/receipt logic. Every OS operation is an injected callback.
// This header has no Win32 calls; tests never register keys or touch a desktop.
#pragma once
#include <cstdint>
namespace vi_escape {
constexpr unsigned HotkeyId=0x5649, Modifiers=0x0007, EndKey=0x23;
struct Rect {long left=0,top=0,right=0,bottom=0;};
inline bool Equal(const Rect& a,const Rect& b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
struct Api {bool ok=false;std::uint32_t error=0;bool errorKnown=false;};
struct Clip {Api query;Rect rectangle{},desktop{};bool desktopKnown=false;};
enum class Witness {Valid,Missing,Dead,PidMismatch,CreationMismatch,ImageUnavailable,ImageMismatch,WindowGone,ThreadMismatch,Destroyed,MetadataChanged,QueryFailed};
inline const char* Name(Witness value){switch(value){case Witness::Valid:return "valid";case Witness::Missing:return "missing";case Witness::Dead:return "dead";case Witness::PidMismatch:return "pid_mismatch";case Witness::CreationMismatch:return "creation_mismatch";case Witness::ImageUnavailable:return "image_unavailable";case Witness::ImageMismatch:return "image_mismatch";case Witness::WindowGone:return "window_gone";case Witness::ThreadMismatch:return "thread_mismatch";case Witness::Destroyed:return "destroy_event_observed";case Witness::MetadataChanged:return "window_metadata_changed";default:return "query_failed";}}
enum class Foreground {Target,External,Unknown};
struct Handback {bool attempted=false;Api call;bool confirmationKnown=false,confirmed=false;const char* reason="not_attempted";std::uint64_t actualWindow=0;std::uint32_t actualPid=0;};
struct Event {bool registered=false,hotkey=false;unsigned id=0,modifiers=0,key=0;};
struct Result {
 bool accepted=false,ok=false,clearAttempted=false,clipConfirmationKnown=false,clipConfirmed=false;
 Api clear;Clip after;Witness target=Witness::Missing,previous=Witness::Missing;Foreground foreground=Foreground::Unknown;Handback handback;
 const char* reason="not_registered";
};
// Callback contract: Clear is global NULL ONLY; HandBack must freshly revalidate
// both original-handle witnesses and current-target foreground immediately before
// the API. No callback may wait on game IPC. No callback cancels virtual input.
template<class Operations> Result Handle(const Event& event,Operations& operations){
 Result out;
 if(!event.registered)return out;
 if(!event.hotkey||event.id!=HotkeyId||event.modifiers!=Modifiers||event.key!=EndKey){out.reason="wrong_hotkey_message";return out;}
 out.accepted=true;out.clearAttempted=true;out.clear=operations.Clear();
 out.after=operations.QueryClip();out.clipConfirmationKnown=out.after.query.ok&&out.after.desktopKnown;
 out.clipConfirmed=out.clear.ok&&out.clipConfirmationKnown&&Equal(out.after.rectangle,out.after.desktop);
 // These read-only observations may fail; none can suppress the explicit clear.
 out.target=operations.Target();out.previous=operations.Previous();out.foreground=operations.CurrentForeground();
 if(out.target!=Witness::Valid)out.reason="target_witness_invalid";
 else if(out.foreground==Foreground::External)out.reason="foreground_already_external";
 else if(out.foreground==Foreground::Unknown)out.reason="foreground_unknown";
 else if(out.previous!=Witness::Valid)out.reason="previous_witness_invalid";
 else {out.handback=operations.HandBack();out.reason=out.handback.reason;}
 const bool external=out.target==Witness::Valid&&out.foreground==Foreground::External;
 out.ok=out.clipConfirmed&&(external||(out.handback.attempted&&out.handback.call.ok&&out.handback.confirmationKnown&&out.handback.confirmed));
 return out;
}
} // namespace vi_escape
