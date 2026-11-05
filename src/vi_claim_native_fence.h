#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>

namespace vi_claim_native {
enum Reason { Allowed=0, LiveClaim=1, Transition=2, Contended=3, NativeInFlight=4 };
struct View { unsigned active=0;bool transition=false;unsigned long long admitted=0,finished=0,deniedLive=0,deniedTransition=0,deniedContention=0,claimRefused=0; };
// No clock, callback or application API runs under this private mutex.
// Admission uses try_lock and never waits for an original call to finish.
class Fence {
 std::mutex mutex_;unsigned active_=0;bool transition_=false;
 std::atomic<unsigned long long> admitted_{0},finished_{0},deniedLive_{0},deniedTransition_{0},deniedContention_{0},claimRefused_{0};
public:
 Reason BeginNative(std::uint64_t now,const std::atomic<std::uint64_t>& expiry){
  std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
  if(!lock.owns_lock()){++deniedContention_;return Contended;}
  if(transition_){++deniedTransition_;return Transition;}
  // Load the deadline inside the same reservation lock as the transition flag.
  if(now<expiry.load(std::memory_order_acquire)){++deniedLive_;return LiveClaim;}
  if(active_==~0u){++deniedContention_;return Contended;}
  ++active_;++admitted_;return Allowed;
 }
 void EndNative(){std::lock_guard<std::mutex> lock(mutex_);if(active_){--active_;++finished_;}}
 Reason BeginClaim(){
  std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
  if(!lock.owns_lock()){++claimRefused_;return Contended;}
  if(transition_){++claimRefused_;return Transition;}
  if(active_){++claimRefused_;return NativeInFlight;}
  transition_=true;return Allowed;
 }
 // The caller must publish the claim deadline before ending a successful transition.
 void EndClaim(){std::lock_guard<std::mutex> lock(mutex_);transition_=false;}
 View Snapshot(){std::lock_guard<std::mutex> lock(mutex_);return {active_,transition_,admitted_.load(),finished_.load(),deniedLive_.load(),deniedTransition_.load(),deniedContention_.load(),claimRefused_.load()};}
};
}
