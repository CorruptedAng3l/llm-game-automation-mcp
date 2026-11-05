#pragma once
#include <cstdint>
namespace vi_present_window {
constexpr std::uint64_t FreshMs=5000;
struct Proof {
 std::uintptr_t window=0,cookie=0;std::uint32_t pid=0,thread=0;std::uint64_t created=0;
 long width=0,height=0;bool top=false,child=false,messageOnly=false,ownedPopup=false,tool=false,dummy=false,visible=false;
};
struct Evidence {Proof proof;std::uint64_t observed=0;unsigned api=0;bool quarantined=false;std::uint64_t completed=0,completedQpc=0;};
inline bool Source(unsigned api){return api==9||api==11||api==12;}
inline bool Eligible(const Proof& p,std::uint32_t pid,std::uint64_t created){return p.window&&pid&&p.pid==pid&&p.thread&&created&&p.created==created&&p.top&&!p.child&&!p.messageOnly&&!p.ownedPopup&&!p.tool&&!p.dummy&&p.width>0&&p.height>0&&p.width<=8192&&p.height<=8192&&(std::uint64_t)p.width*p.height>=200*150;}
inline bool Same(const Proof& a,const Proof& b){return a.window==b.window&&a.pid==b.pid&&a.thread==b.thread&&a.created==b.created&&a.cookie&&a.cookie==b.cookie;}
inline bool Fresh(const Evidence& e,const Proof& current,std::uint32_t pid,std::uint64_t created,std::uint64_t now){return !e.quarantined&&Source(e.api)&&Eligible(current,pid,created)&&Same(e.proof,current)&&now>=e.observed&&now-e.observed<=FreshMs;}
struct Selection {std::uintptr_t window=0;unsigned count=0;bool capacity=false,unconfirmed=false;void Add(const Evidence& e,const Proof& current,std::uint32_t pid,std::uint64_t created,std::uint64_t now){if(!current.visible&&Fresh(e,current,pid,created,now)){++count;window=count==1?current.window:0;}}bool Unique()const{return !capacity&&!unconfirmed&&count==1&&window;}};
}
