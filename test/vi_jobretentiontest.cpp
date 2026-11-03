#include "../src/vi_job_time.h"
#include <cstdio>
int main(){int pass=0,fail=0;auto check=[&](bool good,const char* name){printf("%s %s\n",good?"PASS":"FAIL",name);good?++pass:++fail;};
 check(!vi_job_time::HistoryExpired(2000,2001),"fresh finish after worker snapshot is retained");
 check(!vi_job_time::HistoryExpired(2000,2000),"same-tick finish retained");
 check(!vi_job_time::HistoryExpired(2001,2000),"one-tick-old finish retained");
 check(!vi_job_time::HistoryExpired(62000,2000),"exact60second boundary retained");
 check(vi_job_time::HistoryExpired(62001,2000),"expired terminal record eligible");
 check(!vi_job_time::HistoryExpired(1,~std::uint64_t(0)),"future timestamp cannot unsigned-underflow into expiry");
 printf("%d PASS / %d FAIL\n",pass,fail);return fail?1:0;}
