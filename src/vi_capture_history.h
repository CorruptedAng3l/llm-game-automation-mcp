#pragma once
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
namespace vi_capture_history {
struct View {bool measured=false,comparable=false,changed=false;std::uint64_t identical=0;double unchangedMs=0;};
class History {
 struct Entry {std::string key;std::uint64_t hash=0,qpc=0,since=0,frequency=0,identical=1;};
 std::mutex mutex;std::deque<Entry> entries;
public:
 View Observe(const std::string& key,std::uint64_t hash,std::uint64_t qpc,std::uint64_t frequency){
  if(key.empty()||key.size()>4096||!qpc||!frequency)return {};
  std::lock_guard<std::mutex> lock(mutex);View out;out.measured=true;out.identical=1;
  Entry next{key,hash,qpc,qpc,frequency,1};
  for(auto found=entries.begin();found!=entries.end();++found)if(found->key==key){
   if(found->frequency==frequency&&qpc>found->qpc){out.comparable=true;out.changed=hash!=found->hash;
    if(!out.changed){next.since=found->since;next.identical=found->identical==(std::numeric_limits<std::uint64_t>::max)()?found->identical:found->identical+1;out.identical=next.identical;out.unchangedMs=(double)(qpc-next.since)*1000.0/(double)frequency;}}
   entries.erase(found);break;
  }
  if(entries.size()>=32)entries.pop_front();entries.push_back(std::move(next));return out;
 }
};
}
