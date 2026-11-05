// Lifetime metadata is shared only by interfaces with a verified identical
// controlling IUnknown. No application COM reference is retained by this cache.
thread_local unsigned g_diFormatIdentityDepth=0;
#pragma comment(lib,"dinput8.lib") // Linked standard-format data only; no factory import/call.
#define VI_DI_FORMAT_LIFETIME 1
void* DiDiscoveryOriginalFor(void* object,unsigned slot,DiDiscoveryKind kind){
    const auto target=(*(void***)object)[slot];std::lock_guard<std::mutex> lock(g_diDiscoveryMx);
    for(unsigned i=0;i<kDiDiscoverySlots;++i){const auto& entry=g_diDiscovery[kind][i];if(entry.target==target&&entry.enabled&&CursorEntryReaches(target,kDiDiscoveryDetours[kind][i]))return entry.original.load(std::memory_order_acquire);}
    return nullptr;
}
std::shared_ptr<DiFormatLifetime> DiFormatFind(void* object){std::lock_guard<std::mutex> lock(g_diFormatMx);auto it=g_diFormatAliases.find(object);return it==g_diFormatAliases.end()?nullptr:it->second;}
void DiFormatRetire(const std::shared_ptr<DiFormatLifetime>& lifetime){
    std::lock_guard<std::mutex> lock(g_diFormatMx);if(!lifetime->live)return;lifetime->live=false;++lifetime->revision;
    for(auto it=g_diFormatAliases.begin();it!=g_diFormatAliases.end();)if(it->second==lifetime){if(g_diFormatRetiredAliases.size()<4096)g_diFormatRetiredAliases.insert(it->first);else g_diFormatLegacyUncertain=true;it=g_diFormatAliases.erase(it);}else ++it;++g_diFormatRetired;
}
bool DiCanonicalIdentity(void* object,void*& identity){
    if(g_diFormatIdentityDepth)return false;
    struct Depth{Depth(){++g_diFormatIdentityDepth;}~Depth(){--g_diFormatIdentityDepth;}} depth;
    void** table=*(void***)object;
    if(!DiDiscoveryInstall(table[0],DiQuery)||!DiDiscoveryInstall(table[2],DiRelease))return false;
    const auto query=(DiQueryFn)DiDiscoveryOriginalFor(object,0,DiQuery);if(!query)return false;
    void* unknown=nullptr;if(FAILED(query(object,IID_IUnknown,&unknown))||!unknown)return false;
    void** unknownTable=*(void***)unknown;const bool guarded=DiDiscoveryInstall(unknownTable[0],DiQuery)&&DiDiscoveryInstall(unknownTable[2],DiRelease);
    auto release=(DiReleaseFn)DiDiscoveryOriginalFor(unknown,2,DiRelease);if(!release)release=(DiReleaseFn)unknownTable[2];
    release(unknown);if(!guarded)return false; // Caller retains borrowed self; Release's diagnostic count is not an ownership proof.
    identity=unknown;return true;
}
bool DiFormatEnsure(void* object){
    void* identity=nullptr;if(!DiCanonicalIdentity(object,identity))return false;
    std::lock_guard<std::mutex> lock(g_diFormatMx);
    auto canonical=g_diFormatAliases.find(identity);std::shared_ptr<DiFormatLifetime> lifetime;
    if(canonical!=g_diFormatAliases.end()&&canonical->second->live)lifetime=canonical->second;
    else{if(g_diFormatAliases.size()>4094)return false;lifetime=std::make_shared<DiFormatLifetime>();lifetime->identity=identity;g_diFormatAliases[identity]=lifetime;}
    auto prior=g_diFormatAliases.find(object);if(prior!=g_diFormatAliases.end()&&prior->second!=lifetime)return false;
    if(!g_diFormatAliases.count(object)&&g_diFormatAliases.size()>=4096)return false;g_diFormatAliases[object]=lifetime;g_diFormatRetiredAliases.erase(object);g_diFormatRetiredAliases.erase(identity);return true;
}
bool DiFormatLink(void* source,void* alias){
    const auto lifetime=DiFormatFind(source);if(!lifetime)return DiFormatEnsure(alias);
    void* identity=nullptr;if(!DiCanonicalIdentity(alias,identity)||identity!=lifetime->identity)return false;
    std::lock_guard<std::mutex> lock(g_diFormatMx);auto sourceNow=g_diFormatAliases.find(source);
    if(!lifetime->live||sourceNow==g_diFormatAliases.end()||sourceNow->second!=lifetime||(!g_diFormatAliases.count(alias)&&g_diFormatAliases.size()>=4096))return false;
    g_diFormatAliases[alias]=lifetime;g_diFormatAliases[identity]=lifetime;g_diFormatRetiredAliases.erase(alias);g_diFormatRetiredAliases.erase(identity);return true;
}
bool DiStandardMouseFormat(LPCDIDATAFORMAT input){
    DIDATAFORMAT format{};if(!ReaderBufferLoad(input,&format,sizeof(format)))return false;
    const DIDATAFORMAT* standard=format.dwDataSize==sizeof(DIMOUSESTATE)?&c_dfDIMouse:format.dwDataSize==sizeof(DIMOUSESTATE2)?&c_dfDIMouse2:nullptr;
    if(!standard||format.dwSize!=standard->dwSize||format.dwObjSize!=standard->dwObjSize||format.dwFlags!=standard->dwFlags||format.dwNumObjs!=standard->dwNumObjs||!format.rgodf)return false;
    const auto base=(std::uintptr_t)format.rgodf;if(base>(std::numeric_limits<std::uintptr_t>::max)()-format.dwNumObjs*sizeof(DIOBJECTDATAFORMAT))return false;
    for(unsigned i=0;i<format.dwNumObjs;++i){DIOBJECTDATAFORMAT object{};GUID guid{};const auto& expected=standard->rgodf[i];
        if(!ReaderBufferLoad((const void*)(base+i*sizeof(object)),&object,sizeof(object))||object.dwOfs!=expected.dwOfs||object.dwType!=expected.dwType||object.dwFlags!=expected.dwFlags)return false;
        if(!object.pguid||!expected.pguid){if(object.pguid!=expected.pguid)return false;}
        else if(!ReaderBufferLoad(object.pguid,&guid,sizeof(guid))||!IsEqualGUID(guid,*expected.pguid))return false;
    }
    return true;
}
// GetObjectInfo(DIPH_BYOFFSET) describes the selected native object in its
// standard device layout even when the requested data format permutes storage.
// Restrict that identity proof to GUID_SysMouse; never guess from button instance.
void DiResolveMouseFormat(void* self,vi_di_mouse_format::Plan& plan){
    if(!plan.parsed)return;IDirectInputDevice8W* device=nullptr;
    const auto query=(DiQueryFn)(*(void***)self)[0];
    if(FAILED(query(self,IID_IDirectInputDevice8W,(void**)&device))||!device){plan.reason="mouse_native_metadata_interface_unavailable";return;}
    struct Release{IDirectInputDevice8W* device;~Release(){device->Release();}} release{device};
    DIDEVICEINSTANCEW instance{};instance.dwSize=sizeof(instance);
    if(FAILED(device->GetDeviceInfo(&instance))||!IsEqualGUID(instance.guidInstance,GUID_SysMouse)){plan.reason="custom_mouse_requires_verified_system_mouse";return;}
    for(auto& binding:plan.objects){DIDEVICEOBJECTINSTANCEW object{};object.dwSize=sizeof(object);
        if(FAILED(device->GetObjectInfo(&object,binding.offset,DIPH_BYOFFSET))){plan.reason="mouse_native_object_metadata_unavailable";return;}
        if(binding.button){
            const DWORD type=DIDFT_GETTYPE(object.dwType);
            if(!IsEqualGUID(object.guidType,GUID_Button)||!(type&DIDFT_BUTTON)||(type&~DIDFT_BUTTON)||object.dwOfs<DIMOFS_BUTTON0||object.dwOfs>DIMOFS_BUTTON4||(binding.instance!=DIDFT_GETINSTANCE(DIDFT_ANYINSTANCE)&&binding.instance!=DIDFT_GETINSTANCE(object.dwType))){plan.reason="mouse_native_button_identity_unproved";return;}
            binding.source=object.dwOfs;
        }else{const GUID& guid=binding.source==DIMOFS_X?GUID_XAxis:binding.source==DIMOFS_Y?GUID_YAxis:GUID_ZAxis;
            if(!IsEqualGUID(object.guidType,guid)||object.dwOfs!=binding.source||DIDFT_GETTYPE(object.dwType)!=DIDFT_RELAXIS){plan.reason="mouse_native_relative_axis_identity_unproved";return;}}
    }
    for(size_t i=0;i<plan.objects.size();++i)for(size_t j=0;j<i;++j)if(plan.objects[i].source==plan.objects[j].source){plan.reason="mouse_native_duplicate_object_identity";return;}
    plan.supported=true;
}
HRESULT DiSetFormatCall(DiSetFormatFn original,void* self,LPCDIDATAFORMAT format){
    if(!DiFormatEnsure(self)){++g_diFormatRefused;return DIERR_NOTINITIALIZED;}
    auto lifetime=DiFormatFind(self);if(!lifetime){++g_diFormatRefused;return DIERR_NOTINITIALIZED;}unsigned long long revision=0;{std::lock_guard<std::mutex> lock(g_diFormatMx);revision=lifetime->revision;}
    auto plan=std::make_shared<vi_di_format::Plan>(vi_di_format::Copy(format,ReaderBufferLoad));const int type=DevType(self);const bool mouse=type==2&&DiStandardMouseFormat(format);
    auto mousePlan=type==2?std::make_shared<vi_di_mouse_format::Plan>(vi_di_mouse_format::Copy(format,ReaderBufferLoad)):nullptr;
    const HRESULT hr=original(self,format);if(FAILED(hr))return hr;
    if(mousePlan&&!mouse)DiResolveMouseFormat(self,*mousePlan);
    std::lock_guard<std::mutex> lock(g_diFormatMx);
    if(!lifetime->live||g_diFormatAliases.find(self)==g_diFormatAliases.end()||g_diFormatAliases[self]!=lifetime)return hr;
    if(lifetime->revision!=revision){plan->supported=false;plan->reason="format_changed_during_set";if(mousePlan){mousePlan->supported=false;mousePlan->reason=plan->reason;}}
    lifetime->format=plan;lifetime->mouseFormat=mousePlan;lifetime->mouseStandard=mouse&&lifetime->revision==revision;lifetime->deviceType=type;++lifetime->revision;
    if((type==1&&plan->supported)||lifetime->mouseStandard||(mousePlan&&mousePlan->supported))++g_diFormatsAccepted;else{++g_diFormatsUnsupported;g_diFormatLastUnsupported=mousePlan?mousePlan->reason:plan->reason.empty()?"unvirtualized_device_type":plan->reason;}return hr;
}
HRESULT DiSetActionMapCall(DiActionMapFn original,void* self,void* format,const void* name,DWORD flags){
    if(!DiFormatEnsure(self)){++g_diFormatRefused;return DIERR_NOTINITIALIZED;}
    const auto lifetime=DiFormatFind(self);if(!lifetime){++g_diFormatRefused;return DIERR_NOTINITIALIZED;}const HRESULT hr=original(self,format,name,flags);if(FAILED(hr))return hr;
    auto unknown=std::make_shared<vi_di_format::Plan>();unknown->reason="action_map_layout_unobserved";
    std::lock_guard<std::mutex> lock(g_diFormatMx);if(lifetime->live){lifetime->format=unknown;lifetime->mouseFormat.reset();lifetime->mouseStandard=false;++lifetime->revision;++g_diFormatsUnsupported;g_diFormatLastUnsupported=unknown->reason;}return hr;
}
struct DiFormatReadProof {std::shared_ptr<DiFormatLifetime> lifetime;std::shared_ptr<const vi_di_format::Plan> format;std::shared_ptr<const vi_di_mouse_format::Plan> mouseFormat;unsigned long long revision=0;bool mouse=false,retired=false;};
DiFormatReadProof DiFormatProof(void* self){DiFormatReadProof out;std::lock_guard<std::mutex> lock(g_diFormatMx);auto found=g_diFormatAliases.find(self);if(found!=g_diFormatAliases.end()){out.lifetime=found->second;out.format=out.lifetime->format;out.mouseFormat=out.lifetime->mouseFormat;out.revision=out.lifetime->revision;out.mouse=out.lifetime->mouseStandard;}else out.retired=g_diFormatLegacyUncertain||g_diFormatRetiredAliases.count(self)!=0;return out;}
bool DiFormatProofCurrentLocked(void* self,const DiFormatReadProof& proof){auto found=g_diFormatAliases.find(self);return proof.lifetime&&proof.lifetime->live&&found!=g_diFormatAliases.end()&&found->second==proof.lifetime&&proof.lifetime->revision==proof.revision&&proof.lifetime->format==proof.format;}
bool DiFormatProofCurrent(void* self,const DiFormatReadProof& proof){std::lock_guard<std::mutex> lock(g_diFormatMx);return DiFormatProofCurrentLocked(self,proof);}
bool DiFormattedState(PFN_DIGDS original,void* self,DWORD bytes,LPVOID output,HRESULT& result){
    auto proof=DiFormatProof(self);if(!proof.lifetime&&!proof.retired)return false;const int type=DevType(self);
    if(type==0)return false;
    if(!proof.lifetime){if(!ReaderMask(ReaderDiState)&&!Faking()){result=original(self,bytes,output);return true;}++g_diFormatRefused;result=DIERR_NOTINITIALIZED;return true;}
    if(type==2&&proof.mouse&&proof.format&&bytes==proof.format->bytes&&DiFormatProofCurrent(self,proof))return false;
    if(type==2&&proof.mouseFormat&&proof.mouseFormat->supported&&bytes==proof.mouseFormat->bytes&&DiFormatProofCurrent(self,proof)){
        if(!output||!ReaderBufferWritable(output,bytes)){result=DIERR_INVALIDPARAM;return true;}
        std::vector<BYTE> state(bytes);result=original(self,bytes,state.data());
        if(!DiFormatProofCurrent(self,proof)){++g_diFormatRefused;result=DIERR_NOTINITIALIZED;return true;}
        if(FAILED(result)){if(result==DIERR_INVALIDPARAM||!Faking())return true;std::fill(state.begin(),state.end(),0);result=DI_OK;++C.di_fake;}
        else if(ReaderMask(ReaderDiState))std::fill(state.begin(),state.end(),0);
        std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);std::lock_guard<std::mutex> formatLock(g_diFormatMx);
        if(!DiFormatProofCurrentLocked(self,proof)){++g_diFormatRefused;result=DIERR_NOTINITIALIZED;return true;}
        long long takeX=0,takeY=0,takeZ=0;bool buttons[5]{};BYTE buttonValues[5]{};
        for(const auto& object:proof.mouseFormat->objects){
            if(object.width==sizeof(LONG)){LONG value=0;memcpy(&value,state.data()+object.offset,sizeof(value));
                const long long pending=object.source==DIMOFS_X?g_diDX.load():object.source==DIMOFS_Y?g_diDY.load():g_diDZ.load();
                const long long take=(std::max)(-2147483648LL-value,(std::min)(2147483647LL-value,pending));value=(LONG)((long long)value+take);
                if(object.source==DIMOFS_X)takeX=take;else if(object.source==DIMOFS_Y)takeY=take;else takeZ=take;
                memcpy(state.data()+object.offset,&value,sizeof(value));
            }else{const int button=(int)(object.source-DIMOFS_BUTTON0);auto& value=state[object.offset];if(g_vk[kDiMouseVk[button]])value=0x80;buttons[button]=true;buttonValues[button]=value;
            }
        }
        if(!ReaderBufferStore(output,state.data(),bytes)){result=DIERR_INVALIDPARAM;return true;}
        g_diDX.fetch_sub((decltype(g_diDX.load()))takeX);g_diDY.fetch_sub((decltype(g_diDY.load()))takeY);g_diDZ.fetch_sub((decltype(g_diDZ.load()))takeZ);
        for(int button=0;button<5;++button)if(buttons[button]){auto& observation=g_diMouseObservation[button];const auto generation=observation.press.load(),released=observation.release.load();const bool virtualDown=g_vk[kDiMouseVk[button]]!=0;
            g_keyTrace[kDiMouseVk[button]].di_state.record(buttonValues[button]);observation.state_poll_at=GetTickCount64();
            if(virtualDown&&(buttonValues[button]&0x80))ObserveDiMouseDown(button,generation,false);else if(!virtualDown&&!(buttonValues[button]&0x80))ObserveDiMouseUp(button,released,false);}
        ++C.di_mouse;g_diMouseMovesSincePoll=0;++g_diFormatStateReads;return true;
    }
    if(type!=1||!proof.format||!proof.format->supported||bytes!=proof.format->bytes||!DiFormatProofCurrent(self,proof)){if(!ReaderMask(ReaderDiState)&&!Faking()){result=original(self,bytes,output);return true;}++g_diFormatRefused;result=proof.format?DIERR_UNSUPPORTED:DIERR_NOTINITIALIZED;return true;}
    if(!output||!ReaderBufferWritable(output,bytes)){result=DIERR_INVALIDPARAM;return true;}
    std::vector<BYTE> state(bytes);result=original(self,bytes,state.data());
    if(!DiFormatProofCurrent(self,proof)){++g_diFormatRefused;result=DIERR_NOTINITIALIZED;return true;}
    if(FAILED(result)){if(result==DIERR_INVALIDPARAM||!Faking())return true;std::fill(state.begin(),state.end(),0);result=DI_OK;++C.di_fake;}
    else if(ReaderMask(ReaderDiState))std::fill(state.begin(),state.end(),0);
    const BYTE* scans=DiScanMap();bool down[256]{},mapped[256]{};int values[256]{};for(int vk=7;vk<256;++vk)if(scans[vk]&&g_vk[vk])down[scans[vk]]=true;
    for(const auto& key:proof.format->keys){if(down[key.scan])state[key.offset]=0x80;mapped[key.scan]=true;values[key.scan]|=state[key.offset];}
    if(!ReaderBufferStore(output,state.data(),bytes)){result=DIERR_INVALIDPARAM;return true;}
    for(int vk=7;vk<256;++vk)if(scans[vk]&&mapped[scans[vk]])g_keyTrace[vk].di_state.record(values[scans[vk]]);
    ++C.di_kbd;++g_diFormatStateReads;return true;
}
bool DiFormattedBuffer(PFN_DIGDD original,void* self,DWORD stride,LPDIDEVICEOBJECTDATA output,LPDWORD inout,DWORD flags,HRESULT& result){
    const auto proof=DiFormatProof(self);if(!proof.lifetime&&!proof.retired)return false;const int type=DevType(self);
    if(!proof.lifetime){if(type==0)return false;if(!ReaderMask(ReaderDiBuffer)&&!Faking()){result=original(self,stride,output,inout,flags);return true;}++g_diFormatRefused;result=DIERR_NOTINITIALIZED;return true;}
    if(type==0||(type==2&&proof.mouse&&DiFormatProofCurrent(self,proof)))return false;
    const bool mask=ReaderMask(ReaderDiBuffer);
    const bool customMouse=type==2&&proof.mouseFormat&&proof.mouseFormat->supported;
    if(!((type==1&&proof.format&&proof.format->supported)||customMouse)||!DiFormatProofCurrent(self,proof)){if(!mask&&!Faking()){result=original(self,stride,output,inout,flags);return true;}++g_diFormatRefused;result=proof.format?DIERR_UNSUPPORTED:DIERR_NOTINITIALIZED;return true;}
    if(!inout||(stride!=16&&stride!=sizeof(DIDEVICEOBJECTDATA))||(flags&~DIGDD_PEEK))return false;
    DWORD requested=0;if(!ReaderBufferWritable(inout,sizeof(*inout))||!ReaderBufferLoad(inout,&requested,sizeof(requested))){result=DIERR_INVALIDPARAM;return true;}
    if(customMouse){++C.di_data_mouse;g_diMouseMovesSincePoll=0;}else ++C.di_data_kbd;const size_t capacity=(size_t)requested*stride;
    if(output&&(requested>kReaderScratchLimit/stride||!ReaderBufferWritable(output,capacity))){result=DIERR_INVALIDPARAM;return true;}
    std::vector<BYTE> privateOutput;auto nativeOutput=output;if(mask&&output){privateOutput.resize((std::max)(capacity,(size_t)1));nativeOutput=(LPDIDEVICEOBJECTDATA)privateOutput.data();}
    result=original(self,stride,nativeOutput,inout,flags);DWORD nativeCount=0;
    if(FAILED(result)){if(result==DIERR_INVALIDPARAM||!Faking())return true;nativeCount=0;result=DI_OK;++C.di_fake;}
    else if(!ReaderBufferLoad(inout,&nativeCount,sizeof(nativeCount))){result=DIERR_INVALIDPARAM;return true;}
    if(!DiFormatProofCurrent(self,proof)){++g_diFormatRefused;result=DIERR_NOTINITIALIZED;return true;}
    DWORD have=mask?0:nativeCount;if(output&&have>requested){result=DIERR_INVALIDPARAM;return true;}const DWORD room=have<requested?requested-have:0;const bool peek=(flags&DIGDD_PEEK)!=0;
    std::vector<DiQueuedEvent> events;
    auto offsetFor=[&](DWORD source,DWORD& offset){return customMouse?vi_di_mouse_format::OffsetForSource(*proof.mouseFormat,source,offset):vi_di_format::OffsetForScan(*proof.format,source,offset);};
    auto& queue=customMouse?g_diMouseQ:g_diKbdQ;
    {std::lock_guard<std::mutex> lock(g_diMx);for(const auto& event:queue){DWORD offset=0;if(events.size()<room&&offsetFor(event.first,offset))events.push_back(event);}}
    std::vector<DIDEVICEOBJECTDATA> records;records.reserve(events.size());
    if(output)for(const auto& event:events){DIDEVICEOBJECTDATA record{};offsetFor(event.first,record.dwOfs);record.dwData=event.second;record.dwTimeStamp=event.timestamp;record.dwSequence=event.sequence;
        if(stride>16){DIPROPPOINTER property{};property.diph={sizeof(property),sizeof(DIPROPHEADER),record.dwOfs,DIPH_BYOFFSET};using Get=HRESULT(STDMETHODCALLTYPE*)(void*,REFGUID,LPDIPROPHEADER);const auto get=(Get)(*(void***)self)[5];
            if(!get||FAILED(get(self,DIPROP_APPDATA,&property.diph))){++g_diFormatAppDataUnavailable;++g_diFormatRefused;result=DIERR_NOTINITIALIZED;return true;}record.uAppData=property.uData;}
        records.push_back(record);
    }
    // Native property callbacks were unlocked. Revalidate the exact lifetime and
    // queued tokens before copying/consuming; no uncertain event is replayed.
    std::lock_guard<std::mutex> lock(g_diMx);std::unique_lock<std::mutex> formatLock(g_diFormatMx,std::defer_lock);if(customMouse)formatLock.lock();
    if(customMouse?!DiFormatProofCurrentLocked(self,proof):!DiFormatProofCurrent(self,proof)){++g_diFormatRefused;result=DIERR_NOTINITIALIZED;return true;}
    const BYTE* scans=DiScanMap();
    if(customMouse&&output&&requested&&!peek)g_diMouseBufferPollAt=GetTickCount64();
    for(size_t i=0;i<events.size();++i){const auto& event=events[i];auto found=std::find_if(queue.begin(),queue.end(),[&](const DiQueuedEvent& current){return current.first==event.first&&current.second==event.second&&current.timestamp==event.timestamp&&current.sequence==event.sequence&&current.generation==event.generation;});if(found==queue.end())continue;
        if(output&&!ReaderBufferStore((BYTE*)output+(size_t)have*stride,&records[i],stride)){result=DIERR_INVALIDPARAM;return true;}
        if(!peek){if(output){if(customMouse){if(event.first>=DIMOFS_BUTTON0&&event.first<=DIMOFS_BUTTON4){const int button=(int)(event.first-DIMOFS_BUTTON0);g_keyTrace[kDiMouseVk[button]].di_buffer.record((int)event.second);if(event.second&0x80)ObserveDiMouseDown(button,event.generation,true);else ObserveDiMouseUp(button,event.generation,true);}}else for(int vk=7;vk<256;++vk)if(scans[vk]&&scans[vk]==event.first)g_keyTrace[vk].di_buffer.record((int)event.second);}queue.erase(found);}++have;
    }
    if(!ReaderBufferStore(inout,&have,sizeof(have))){result=DIERR_INVALIDPARAM;return true;}++g_diFormatBufferReads;return true;
}
