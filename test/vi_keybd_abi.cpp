#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <type_traits>
#include "keybd_signature.inl"
static_assert(std::is_same_v<PFN_KeybdEvent,decltype(&::keybd_event)>);
static_assert(sizeof(BYTE)==1&&sizeof(DWORD)==4&&sizeof(ULONG_PTR)==sizeof(void*));
int main(){std::puts("RESULT 2 PASS 0 FAIL");return 0;}
