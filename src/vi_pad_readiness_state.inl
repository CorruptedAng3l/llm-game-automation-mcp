// All fields share g_padStateMx. These observations describe API returns,
// never a device arrival event, UI glyph change, or application action.
unsigned long long g_padPublication=1;
struct PadReturnObservation {
 unsigned long long count=0,standard=0,extended=0,fast=0,publication=0;
 ULONGLONG at=0;DWORD packet=0;bool neutral=false;
} g_padReturn;
void PadPublicationChangedLocked(){if(!++g_padPublication)++g_padPublication;}
void PadRecordReturnLocked(int slot,const XST& state,bool fast){
 ++g_padReturn.count;if(slot<4)++g_padReturn.standard;else ++g_padReturn.extended;
 if(fast)++g_padReturn.fast;g_padReturn.publication=g_padPublication;
 g_padReturn.at=GetTickCount64();g_padReturn.packet=state.pkt;
 const auto& p=state.gp;g_padReturn.neutral=!p.b&&!p.lt&&!p.rt&&!p.lx&&!p.ly&&!p.rx&&!p.ry;
}
void PadMergeLocked(int slot,XST& state){
 const int mask=g_padSetMask;
 state.gp.b|=(WORD)(slot<4?(g_padButtons&~0x400):g_padButtons);
 if(mask&1)state.gp.lt=(BYTE)g_padLT;if(mask&2)state.gp.rt=(BYTE)g_padRT;
 if(mask&4)state.gp.lx=(SHORT)g_padLX;if(mask&8)state.gp.ly=(SHORT)g_padLY;
 if(mask&16)state.gp.rx=(SHORT)g_padRX;if(mask&32)state.gp.ry=(SHORT)g_padRY;
 state.pkt=g_padPacket;
}
