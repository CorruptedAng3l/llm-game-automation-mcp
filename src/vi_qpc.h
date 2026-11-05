#pragma once
#include <windows.h>
#include <cstdint>
// Shared monotonic sample domain for target input and frame/capture receipts.
// Wall time and housekeeping/claim deadlines retain their separate contracts.
namespace vi_qpc {
inline std::uint64_t Frequency(){static const std::uint64_t value=[](){LARGE_INTEGER n{};return QueryPerformanceFrequency(&n)&&n.QuadPart>0?(std::uint64_t)n.QuadPart:0;}();return value;}
inline std::uint64_t Now(){LARGE_INTEGER n{};return QueryPerformanceCounter(&n)&&n.QuadPart>0?(std::uint64_t)n.QuadPart:0;}
inline std::uint64_t Milliseconds(std::uint64_t ms,std::uint64_t frequency){return frequency/1000*ms+(frequency%1000)*ms/1000;}
inline double DeltaMs(std::uint64_t end,std::uint64_t start,std::uint64_t frequency){if(!frequency)return 0;return (end>=start?(double)(end-start):-(double)(start-end))*1000.0/(double)frequency;}
}
