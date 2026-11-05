#pragma once
#include <cstdint>
#include <limits>
namespace vi_cli_deadline {
inline std::uint64_t End(std::uint64_t now,std::uint64_t duration){const auto maximum=(std::numeric_limits<std::uint64_t>::max)();return duration>maximum-now?maximum:now+duration;}
inline std::uint32_t Remaining(std::uint64_t deadline,std::uint64_t now,std::uint32_t cap){if(!deadline)return cap;if(now>=deadline)return 0;const auto left=deadline-now;return left<cap?(std::uint32_t)left:cap;}
inline std::uint32_t Optional(std::uint64_t deadline,std::uint64_t now,std::uint32_t requested,std::uint32_t reserve){const auto left=Remaining(deadline,now,(std::numeric_limits<std::uint32_t>::max)());return left>reserve?(requested<left-reserve?requested:left-reserve):0;}
// A timed-out/failed wait never establishes that a remote loader stopped reading.
inline bool RemoteStorageMayBeFreed(bool threadCreated,bool waitCompleted){return !threadCreated||waitCompleted;}
}
