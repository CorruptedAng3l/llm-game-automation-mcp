#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace vi_sequence_edges {
constexpr std::size_t Limit=8192;
struct Edge {unsigned step=0,line=0,vk=0;bool down=false;std::uint64_t qpc=0,present=0;};
struct Trace {std::vector<Edge> edges;std::uint64_t dropped=0;};
inline void Record(Trace& trace,unsigned step,unsigned line,unsigned vk,bool down,std::uint64_t qpc,std::uint64_t present){
 if(trace.edges.size()>=Limit){++trace.dropped;return;}trace.edges.push_back({step,line,vk,down,qpc,present});
}
}
