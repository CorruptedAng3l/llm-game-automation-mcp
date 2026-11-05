#include "vi_claim_native_fence.h"
vi_claim_native::Fence g_claimNativeFence;
std::atomic<unsigned long long> g_claimCleanupRenewalSkipped{0};
bool AgentClaimLive(){return HousekeepingNow()<g_jobClaimExpiry.load(std::memory_order_acquire);}
class ClaimNativeOperation {
 vi_claim_native::Reason reason_;
public:
 ClaimNativeOperation():reason_(g_claimNativeFence.BeginNative(HousekeepingNow(),g_jobClaimExpiry)){}
 ~ClaimNativeOperation(){if(reason_==vi_claim_native::Allowed)g_claimNativeFence.EndNative();}
 ClaimNativeOperation(const ClaimNativeOperation&)=delete;ClaimNativeOperation& operator=(const ClaimNativeOperation&)=delete;
 explicit operator bool()const{return reason_==vi_claim_native::Allowed;}
 unsigned Reason()const{return (unsigned)reason_;}
};
class ClaimNativeAdmission {
 vi_claim_native::Reason reason_;
public:
 ClaimNativeAdmission():reason_(g_claimNativeFence.BeginClaim()){}
 ~ClaimNativeAdmission(){if(reason_==vi_claim_native::Allowed)g_claimNativeFence.EndClaim();}
 ClaimNativeAdmission(const ClaimNativeAdmission&)=delete;ClaimNativeAdmission& operator=(const ClaimNativeAdmission&)=delete;
 explicit operator bool()const{return reason_==vi_claim_native::Allowed;}
};
std::string ClaimNativeAdmissionRefusal(){
 const auto state=g_claimNativeFence.Snapshot();
 return "{\"ok\":false,\"claim_changed\":false,\"claim_native_operation_pending\":true,\"native_operations_inflight\":"+std::to_string(state.active)+
  ",\"error\":\"claim or renewal refused while a native operation or another claim transition is pending; no claim state was changed; retry only after inspecting cursor_guard.claim_write_guard\"}";
}
