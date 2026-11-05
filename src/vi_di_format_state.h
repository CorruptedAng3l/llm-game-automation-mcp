#pragma once
#include "vi_di_keyboard_format.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
struct DiFormatLifetime {
    void* identity=nullptr;bool live=true;unsigned long long revision=0;
    std::shared_ptr<const vi_di_format::Plan> format;bool mouseStandard=false;int deviceType=-1;
};
std::mutex g_diFormatMx;std::map<void*,std::shared_ptr<DiFormatLifetime>> g_diFormatAliases;
std::set<void*> g_diFormatRetiredAliases;bool g_diFormatLegacyUncertain=false;
std::atomic<unsigned long long> g_diFormatsAccepted{0},g_diFormatsUnsupported{0},g_diFormatStateReads{0},g_diFormatBufferReads{0},g_diFormatRefused{0},g_diFormatRetired{0},g_diLegacyFormatPolls{0};
std::atomic<unsigned long long> g_diFormatAppDataUnavailable{0};
std::string g_diFormatLastUnsupported;
using DiSetFormatFn=HRESULT(STDMETHODCALLTYPE*)(void*,LPCDIDATAFORMAT);
using DiReleaseFn=ULONG(STDMETHODCALLTYPE*)(void*);
using DiActionMapFn=HRESULT(STDMETHODCALLTYPE*)(void*,void*,const void*,DWORD);
bool DiFormatEnsure(void*);
bool DiFormatLink(void*,void*);
std::shared_ptr<DiFormatLifetime> DiFormatFind(void*);
void DiFormatRetire(const std::shared_ptr<DiFormatLifetime>&);
HRESULT DiSetFormatCall(DiSetFormatFn,void*,LPCDIDATAFORMAT);
HRESULT DiSetActionMapCall(DiActionMapFn,void*,void*,const void*,DWORD);
bool DiFormattedState(PFN_DIGDS,void*,DWORD,LPVOID,HRESULT&);
bool DiFormattedBuffer(PFN_DIGDD,void*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD,HRESULT&);
