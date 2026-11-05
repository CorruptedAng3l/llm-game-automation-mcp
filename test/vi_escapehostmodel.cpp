// Exact production Handle with injected callbacks only. No windows.h/user32,
// hotkey registration, desktop querying, physical input or target process.
#include "../src/vi_escape_host.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace vi_escape;
namespace {
int passed=0,failed=0;
void Check(bool good,const char* name){if(good)++passed;else{++failed;std::printf("FAIL %s\n",name);}}
struct Fake {
 unsigned clears=0,queries=0,targetReads=0,previousReads=0,foregroundReads=0,handbacks=0,nativeForegroundWrites=0,ipcCalls=0;
 Api clear{true,0,false};Clip clip{{true,0,false},{-100,-100,2000,1200},{-100,-100,2000,1200},true};
 Witness target=Witness::Valid,previous=Witness::Valid;Foreground foreground=Foreground::Target;
 Handback handback{true,{true,0,false},true,true,"foreground_confirmed"};
 Api Clear(){++clears;return clear;}Clip QueryClip(){++queries;return clip;}
 Witness Target(){++targetReads;return target;}Witness Previous(){++previousReads;return previous;}
 Foreground CurrentForeground(){++foregroundReads;return foreground;}
 Handback HandBack(){++handbacks;if(handback.attempted)++nativeForegroundWrites;return handback;}
};
Event Correct(){return {true,true,HotkeyId,Modifiers,EndKey};}
bool NoCalls(const Fake& f){return !f.clears&&!f.queries&&!f.targetReads&&!f.previousReads&&!f.foregroundReads&&!f.handbacks&&!f.ipcCalls;}
}
int main(int argc,char** argv){
 if(argc==2&&std::strcmp(argv[1],"--await-start")==0){char line[8]{};if(!std::fgets(line,sizeof(line),stdin)||std::strcmp(line,"RUN\n")!=0)return 2;}
 else if(argc!=1)return 2;
 {Fake f;const auto r=Handle(Correct(),f);Check(r.ok&&r.accepted&&r.clearAttempted&&r.clipConfirmed&&r.handback.confirmed&&f.clears==1&&f.queries==1&&f.nativeForegroundWrites==1,"accepted message clears and verifies then returns validated foreground");}
 for(unsigned bad=0;bad<5;++bad){Fake f;auto e=Correct();if(bad==0)e.registered=false;if(bad==1)e.hotkey=false;if(bad==2)++e.id;if(bad==3)e.modifiers=0x0003;if(bad==4)e.key=0x24;const auto r=Handle(e,f);Check(!r.accepted&&!r.ok&&NoCalls(f),"unregistered/wrong ID/message/chord/key causes no OS callback");}
 for(auto state:{Witness::Missing,Witness::Dead,Witness::PidMismatch,Witness::CreationMismatch,Witness::ImageUnavailable,Witness::ImageMismatch,Witness::QueryFailed}){Fake f;f.target=state;const auto r=Handle(Correct(),f);Check(r.accepted&&r.target==state&&!r.ok&&f.clears==1&&f.queries==1&&f.nativeForegroundWrites==0,"invalid target never suppresses global human-message clear and never authorizes handback");}
 for(auto state:{Witness::Missing,Witness::Dead,Witness::PidMismatch,Witness::CreationMismatch,Witness::ImageUnavailable,Witness::ImageMismatch,Witness::WindowGone,Witness::ThreadMismatch,Witness::Destroyed,Witness::MetadataChanged,Witness::QueryFailed}){Fake f;f.previous=state;const auto r=Handle(Correct(),f);Check(r.accepted&&r.previous==state&&!r.ok&&r.clipConfirmed&&f.clears==1&&f.nativeForegroundWrites==0,"missing/stale/denied previous witness clears but never guesses foreground");}
 {Fake f;f.foreground=Foreground::Unknown;const auto r=Handle(Correct(),f);Check(!r.ok&&r.clipConfirmed&&f.clears==1&&!f.handbacks,"unknown actual foreground never authorizes handback");}
 {Fake f;f.foreground=Foreground::External;f.previous=Witness::Missing;const auto r=Handle(Correct(),f);Check(r.ok&&r.clipConfirmed&&!f.handbacks&&std::strcmp(r.reason,"foreground_already_external")==0,"already external foreground is left alone while explicit clear is checked");}
 {Fake f;f.clear={false,5,true};const auto r=Handle(Correct(),f);Check(!r.ok&&!r.clipConfirmed&&r.clear.errorKnown&&r.clear.error==5&&f.queries==1&&f.nativeForegroundWrites==1,"denied clear is reported and does not suppress independent handback");}
 {Fake f;f.clip.query={false,5,true};const auto r=Handle(Correct(),f);Check(!r.ok&&!r.clipConfirmationKnown&&!r.clipConfirmed&&f.nativeForegroundWrites==1,"failed clip query remains unknown while handback can still run");}
 {Fake f;f.clip.desktopKnown=false;const auto r=Handle(Correct(),f);Check(!r.ok&&!r.clipConfirmationKnown&&f.nativeForegroundWrites==1,"unknown desktop bounds never manufacture clear confirmation");}
 {Fake f;f.clip.rectangle={20,30,40,50};const auto r=Handle(Correct(),f);Check(!r.ok&&r.clipConfirmationKnown&&!r.clipConfirmed&&f.nativeForegroundWrites==1,"success API with confined readback is not reported free");}
 {Fake f;f.handback={true,{false,0,false},true,false,"foreground_api_denied"};const auto r=Handle(Correct(),f);Check(!r.ok&&r.clipConfirmed&&r.handback.attempted&&!r.handback.call.ok&&!r.handback.call.errorKnown,"foreground denial does not invent an undocumented last-error value");}
 {Fake f;f.handback.confirmed=false;const auto r=Handle(Correct(),f);Check(!r.ok&&r.handback.call.ok&&r.handback.confirmationKnown&&!r.handback.confirmed,"successful foreground API is independently confirmed");}
 {Fake f;f.handback.confirmationKnown=false;f.handback.confirmed=false;const auto r=Handle(Correct(),f);Check(!r.ok&&r.handback.call.ok&&!r.handback.confirmationKnown,"unreadable post-call foreground stays unknown");}
 {Fake f;f.handback={false,{},false,false,"scope_changed_before_handback"};const auto r=Handle(Correct(),f);Check(!r.ok&&!r.handback.attempted&&f.handbacks==1&&!f.nativeForegroundWrites,"fresh adapter scope refusal performs no foreground API");}
 {Fake f;const auto r=Handle(Correct(),f);Check(r.ok&&f.ipcCalls==0,"handler has no game IPC/release/termination callback dependency");}
 std::printf("escape host callbacks: %d PASS / %d FAIL\n",passed,failed);return failed?1:0;
}
