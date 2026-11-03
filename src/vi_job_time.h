#pragma once
#include <cstdint>
namespace vi_job_time {
// A worker can sample now immediately before another path records completion.
// Never treat that future completion timestamp as an enormous unsigned age.
inline bool HistoryExpired(std::uint64_t now,std::uint64_t finished){return now>=finished&&now-finished>60000;}
}
