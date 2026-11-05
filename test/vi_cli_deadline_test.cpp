#include "../src/vi_cli_deadline.h"
#include <cstdio>
int main(){unsigned pass=0,fail=0;auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++pass:++fail;};
 check(vi_cli_deadline::Remaining(0,100,200)==200,"standalone stage keeps its declared cap");
 check(vi_cli_deadline::Remaining(200,100,500)==100,"stage clips to the shared deadline");
 check(vi_cli_deadline::Remaining(200,100,20)==20,"shorter stage cap remains shorter");
 check(vi_cli_deadline::Remaining(200,200,20)==0,"exact deadline permits no new wait");
 check(vi_cli_deadline::Remaining(200,300,20)==0,"expired deadline cannot underflow");
 check(vi_cli_deadline::Remaining(200,100,0)==0,"zero stage budget stays zero");
 check(vi_cli_deadline::End(100,200)==300,"absolute deadline includes prior work");
 const auto maximum=(std::numeric_limits<std::uint64_t>::max)();check(vi_cli_deadline::End(maximum-10,20)==maximum,"deadline addition saturates");
 check(vi_cli_deadline::Remaining(vi_cli_deadline::End(100,500),450,500)==150,"later launch stages do not reset the shared budget");
 check(vi_cli_deadline::Optional(8000,3000,5000,1500)==3500,"eight-second launch clips five-second observation and preserves final checks");
 check(vi_cli_deadline::Optional(8000,6500,5000,1500)==0,"optional guard stops at finalization reserve");
 check(vi_cli_deadline::Optional(8000,8000,5000,1500)==0&&vi_cli_deadline::Optional(8000,9000,5000,1500)==0,"expired launch never borrows finalization time");
 check(vi_cli_deadline::Optional(8000,3000,20,1500)==20&&vi_cli_deadline::Optional(8000,3000,0,1500)==0,"short and zero optional observations retain their bounds");
 check(vi_cli_deadline::Optional(8000,6000,5000,1500)==500,"later optional stages recompute after earlier settling");
 check(!vi_cli_deadline::RemoteStorageMayBeFreed(true,false),"unconfirmed remote thread keeps its argument storage");
 check(vi_cli_deadline::RemoteStorageMayBeFreed(true,true),"confirmed remote completion permits argument cleanup");
 check(vi_cli_deadline::RemoteStorageMayBeFreed(false,false),"a never-started remote thread permits argument cleanup");
 printf("CLI_BUDGET_RESULT %u %u\n",pass,fail);return fail?1:0;}
