#pragma once
#include <deque>
#include <map>
#include <memory>
#include <tuple>
struct WndProcBinding;
struct RawBufferedPacket {
    RAWINPUT record{};HWND window=nullptr;DWORD thread=0;ULONG_PTR stamp=0;ULONGLONG expires=0;
    std::shared_ptr<WndProcBinding> selected;
};
std::mutex g_rawQMx;std::deque<RawBufferedPacket> g_rawQ;
struct RawBufferReader {ULONGLONG seen=0;HANDLE thread=nullptr;~RawBufferReader(){if(thread)CloseHandle(thread);}};
std::map<DWORD,std::shared_ptr<RawBufferReader>> g_rawBufferReaders;
std::atomic<DWORD> g_lastRIB{0}; // aggregate diagnostic only; never selects another thread's route
std::atomic<unsigned long long> g_rawBufferRevoked{0},g_rawBufferForeignThreadChecks{0};
bool RawBufferRecent(DWORD);
void RawBufferReaderSeen(DWORD);
int RawBufferAdmit(const RAWINPUT&,HWND);
void RawBufferPruneLocked(ULONGLONG);
