#include "../src/vi_cursor_guard.h"
#include <cstdio>
int main(){unsigned passed=0,failed=0;for(unsigned flags=0;flags<64;flags++){
 vi_cursor_guard::Owner owner{(flags&1)!=0,(flags&2)!=0,(flags&4)!=0,(flags&8)!=0,(flags&16)!=0,(flags&32)!=0,(flags&8)!=0,false};
 const bool expected=(flags&1)&&(flags&2)&&(flags&4)&&((flags&8)||((flags&16)&&!(flags&32)));
 const bool virtualRead=!(flags&1)||!(flags&2)||((flags&32)&&!(flags&8));
 const bool correct=vi_cursor_guard::NativeWriteAllowed(owner)==expected&&vi_cursor_guard::NativeClipAllowed(owner)==((flags&1)&&(flags&2)&&(flags&4))&&vi_cursor_guard::ReaderAllowed(owner)==((flags&1)&&(flags&2))&&vi_cursor_guard::VirtualPositionReads(owner)==virtualRead;if(correct)++passed;else++failed;
 }
 auto check=[&](bool value){if(value)++passed;else++failed;};vi_cursor_guard::Arbitration epochs;check(epochs.AdmitAgent(1,true));epochs.Human(2);check(epochs.Holding());
 vi_cursor_guard::Owner sticky{true,true,true,false,false,true,true,false};check(!vi_cursor_guard::VirtualPositionReads(sticky)&&!vi_cursor_guard::NativeWriteAllowed(sticky));
 check(!epochs.AdmitAgent(1,true)&&epochs.Holding());check(!epochs.AdmitAgent(3,false)&&epochs.Holding());check(!epochs.AdmitAgent(4,false)&&epochs.Holding());
 check(epochs.AdmitAgent(5,true)&&!epochs.Holding()&&epochs.Automation());sticky.humanHolding=false;sticky.agentPosition=true;sticky.forcedVirtual=false;check(vi_cursor_guard::VirtualPositionReads(sticky));
 epochs.Human(6);check(epochs.Holding()&&!epochs.Automation());sticky.humanHolding=true;sticky.agentPosition=false;check(!vi_cursor_guard::VirtualPositionReads(sticky));
 sticky.foregroundOwned=false;check(vi_cursor_guard::VirtualPositionReads(sticky)&&!vi_cursor_guard::NativeWriteAllowed(sticky));
 std::printf("%u PASS / %u FAIL owner/lease/holding/accepted-agent epochs; Clip permission requires verified ready foreground (geometry checked by hook)\n",passed,failed);return failed?1:0;
}
