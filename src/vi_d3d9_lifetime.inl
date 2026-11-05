// Called outside loader notifications. Retain the exact module before inspecting
// or executing factory code. A same-base reload without our hook is not coverage.
std::atomic<unsigned long long> g_d9LifetimeFailures{0};
bool D9Retain(unsigned api){
    const HMODULE expected=g_d9Module.load();HMODULE held=nullptr;
    const auto fail=[&](){if(held)FreeLibrary(held);++g_d9LifetimeFailures;return false;};
    if(api>1||!expected||GetModuleHandleW(L"d3d9.dll")!=expected)return fail();
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)expected,&held)||held!=expected)return fail();
    const auto target=g_d9FactoryTargets[api].load();
    const auto detour=api?(void*)&h_D9FactoryEx:(void*)&h_D9Factory;
    if(GetModuleHandleW(L"d3d9.dll")!=held||!target||
       (void*)GetProcAddress(held,api?"Direct3DCreate9Ex":"Direct3DCreate9")!=target||!CursorEntryReaches(target,detour))return fail();
    HMODULE empty=nullptr;
    if(!g_d9Retained.compare_exchange_strong(empty,held)){if(empty!=held)return fail();FreeLibrary(held);}
    return true;
}
