MinHook - The Minimalistic API Hooking Library for x64/x86, by Tsuda Kageyu (BSD 2-Clause, see LICENSE.txt and
AUTHORS.txt). Includes the Hacker Disassembler Engine 32/64 by Vyacheslav Patkov (BSD 2-Clause, in LICENSE.txt).

Vendored unmodified: include\MinHook.h, src\*.c, src\*.h, src\hde\* from https://github.com/TsudaKageyu/minhook
at commit 8af6b4acae5a9388fd742b56fa79ece89d96f823.

build.bat compiles it twice: once force-including src\vi_mh_rename.h, which renames every MH_* symbol to VIMH_*, so
vinput keeps a PRIVATE MinHook instance that can be linked next to a host DLL's own MinHook; and once unrenamed for
the compiled-in test host (test\compilein_test.cpp).
