// Refusal and probe admission have one atomic order. A previously admitted COM
// callback may finish; no new callback is admitted after the stop-bit commit.
// Never wait for native callbacks or hold a mutex across them.
std::atomic<unsigned> g_d9ProbeAdmissions{0}; // bit0 stopped; upper bits active reservations
struct D9ProbeNativeAdmission {
    bool admitted=false;
    D9ProbeNativeAdmission(){
        if(g_fullscreenPolicy.load()==1||g_d9Refused.load()>0)return;
        unsigned value=g_d9ProbeAdmissions.load();
        while(!(value&1u))if(g_d9ProbeAdmissions.compare_exchange_weak(value,value+2u)){admitted=true;return;}
    }
    ~D9ProbeNativeAdmission(){if(admitted)g_d9ProbeAdmissions.fetch_sub(2u);}
    explicit operator bool()const{return admitted;}
};
