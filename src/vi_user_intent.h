#pragma once
#include <windows.h>
#include <atomic>

// Target-bound physical selection evidence, shared by the DLL guard and launch watcher.
// No global input hook, input capture, or input logging: only original API samples and OS UI events.
namespace vi_user_intent {
inline constexpr wchar_t kProof[] = L"vinput.user-intent.time.v1";
inline constexpr wchar_t kKind[] = L"vinput.user-intent.kind.v1";
inline constexpr wchar_t kActive[] = L"vinput.user-intent.active.v1";
inline constexpr wchar_t kHeartbeat[] = L"vinput.user-intent.heartbeat.v1";
inline bool BeforeOrEqual(DWORD a,DWORD b) { return (LONG)(b-a)>=0; }
inline bool Matches(HWND window,HWND target,DWORD pid) {
    if(!window || !pid)return false;
    DWORD owner=0;GetWindowThreadProcessId(window,&owner);
    DWORD targetOwner=0;if(target)GetWindowThreadProcessId(target,&targetOwner);
    return owner==pid || (targetOwner==pid && GetAncestor(target,GA_ROOT)==GetAncestor(window,GA_ROOT));
}
inline bool ShellIntermediary(HWND window,HWND shell) {
    if(!window || !shell)return false;
    DWORD owner=0,shellOwner=0;GetWindowThreadProcessId(window,&owner);GetWindowThreadProcessId(shell,&shellOwner);
    if(!owner || owner!=shellOwner)return false;
    LONG_PTR style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if(!IsWindowVisible(window))return (style&(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW))==(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);
    // Measured on the Windows shell: Alt+Tab temporarily foregrounds this visible
    // tool window between two hidden ForegroundStaging windows. Ordinary Explorer
    // windows remain real choices, including every other visible shell class.
    wchar_t name[64]{};GetClassNameW(window,name,64);
    return (style&WS_EX_TOOLWINDOW) && !(style&WS_EX_APPWINDOW) && !lstrcmpW(name,L"MultitaskingViewFrame");
}
struct Physical {
    SHORT (WINAPI* key)(int)=nullptr;
    BOOL (WINAPI* cursor)(LPPOINT)=nullptr;
    HWND (WINAPI* hit)(POINT)=nullptr;
    HWND (WINAPI* foreground)()=nullptr;
    DWORD (WINAPI* ticks)()=nullptr;
    BOOL (WINAPI* lastInput)(PLASTINPUTINFO)=nullptr;
    HWND (WINAPI* shell)()=nullptr;
};
class Detector {
    Physical api;
    SRWLOCK publicationLock=SRWLOCK_INIT;
    struct PublicationGuard {SRWLOCK* lock;explicit PublicationGuard(SRWLOCK* value):lock(value){AcquireSRWLockExclusive(lock);}~PublicationGuard(){ReleaseSRWLockExclusive(lock);}};
    std::atomic<HWND> target{nullptr};std::atomic<DWORD> pid{0};
    std::atomic<unsigned> generation{0};unsigned observedGeneration=~0u;
    std::atomic<unsigned> lossEpoch{0};unsigned observedLossEpoch=~0u;
    std::atomic<bool> active{false};std::atomic<int> why{0};std::atomic<long> gains{0};
    std::atomic<DWORD> altChordAt{0},gainAt{0},switchPreviewAt{0};
    std::atomic<long> switchStarts{0},switchEnds{0};
    std::atomic<bool> provisional{false};std::atomic<DWORD> provisionalUntil{0};
    bool altBefore=false,tabBefore=false,gesture=false,choiceKnown=false,choiceIsTarget=false,altExpired=false;
    HWND gestureOrigin=nullptr,lastForeground=nullptr;DWORD gestureAt=0,chordSessionAt=0,releasedAt=0;int releasePolls=0;
    bool sampled=false,eventWasOurs=false,pollWasOurs=false;unsigned buttonsBefore=0;
    std::atomic<DWORD> hookThread{0};std::atomic<HWINEVENTHOOK> switchHook{nullptr},foregroundHook{nullptr};
    inline static thread_local Detector* listener=nullptr;
    static void CALLBACK Event(HWINEVENTHOOK,DWORD event,HWND window,LONG,LONG,DWORD,DWORD time) {
        if(listener)listener->OnEvent(event,window,time);
    }
    DWORD Now() const {return api.ticks?api.ticks():GetTickCount();}
    bool RecentInput(DWORD now,DWORD maximum=200) const {
        LASTINPUTINFO last{sizeof(last)};return (api.lastInput?api.lastInput(&last):GetLastInputInfo(&last)) && now-last.dwTime<=maximum;
    }
    unsigned Buttons() const {
        if(!api.key)return 0;
        return ((api.key(VK_LBUTTON)&0x8000)?1u:0u)|((api.key(VK_RBUTTON)&0x8000)?2u:0u)|((api.key(VK_MBUTTON)&0x8000)?4u:0u);
    }
    bool HitTarget() const {
        POINT point{};return api.cursor && api.hit && api.cursor(&point) && Owns(api.hit(point));
    }
    void PublishTo(HWND h,DWORD expectedPid,int kind,DWORD time,bool selection=false,unsigned expectedLoss=0) {
        PublicationGuard guard(&publicationLock);
        if(selection && expectedLoss!=lossEpoch.load())return;
        if(selection && api.foreground && !Matches(api.foreground(),h,expectedPid))return;
        DWORD owner=0;GetWindowThreadProcessId(h,&owner);if(!h || owner!=expectedPid)return;
        SetPropW(h,kKind,(HANDLE)(ULONG_PTR)kind);SetPropW(h,kProof,(HANDLE)(ULONG_PTR)(time?time:1));
    }
    void PublishSelection(int kind,DWORD time,unsigned epoch) {PublishTo(target.load(),pid.load(),kind,time,true,epoch);}
    void ResetOwnerState() {
        unsigned current=generation.load(),loss=lossEpoch.load();if(observedGeneration==current && observedLossEpoch==loss)return;
        observedGeneration=current;observedLossEpoch=loss;gesture=choiceKnown=choiceIsTarget=altExpired=false;
        altBefore=api.key && (api.key(VK_MENU)&0x8000);tabBefore=api.key && (api.key(VK_TAB)&0x8000);
        gestureOrigin=lastForeground=nullptr;gestureAt=chordSessionAt=releasedAt=0;releasePolls=0;
        sampled=eventWasOurs=pollWasOurs=false;buttonsBefore=Buttons();
    }
    void PhysicalSelection(HWND foreground,DWORD now,unsigned epoch) {
        bool alt=api.key && (api.key(VK_MENU)&0x8000),tab=api.key && (api.key(VK_TAB)&0x8000);
        bool neutral=(gesture || (alt && tab)) && ShellIntermediary(foreground,api.shell?api.shell():GetShellWindow());
        if(alt && !altBefore){gesture=false;choiceKnown=false;choiceIsTarget=false;altExpired=false;chordSessionAt=0;gestureOrigin=lastForeground?lastForeground:foreground;releasedAt=0;}
        if(alt && tab && !tabBefore && !altExpired && RecentInput(now)) {
            if(gesture)gestureOrigin=lastForeground;
            if(!chordSessionAt)chordSessionAt=now;
            gesture=true;gestureAt=chordSessionAt;releasedAt=0;releasePolls=0;choiceKnown=false;choiceIsTarget=false;altChordAt=now;
        }
        if(gesture && now-gestureAt>5000){gesture=false;releasedAt=0;altExpired=true;altChordAt=0;switchPreviewAt=0;}
        if(gesture && !choiceKnown && foreground && !neutral && GetAncestor(foreground,GA_ROOT)!=GetAncestor(gestureOrigin,GA_ROOT)) {
            choiceKnown=true;choiceIsTarget=Owns(foreground);
        }
        if(gesture && !alt && altBefore){releasedAt=now;releasePolls=0;}
        if(gesture && releasedAt) {
            if(now-releasedAt>100){gesture=false;releasedAt=0;altChordAt=0;}
            else if(foreground && !Owns(foreground) && !neutral){gesture=false;releasedAt=0;altChordAt=0;}
            else if(Owns(foreground) && choiceIsTarget && ++releasePolls>=2) {
                // Some Windows shells consume SWITCHEND and Alt-release messages. Finalize only after a
                // physically observed chord's first changed target stays selected across the Alt-release edge.
                PublishSelection(3,now,epoch);gesture=false;releasedAt=0;altChordAt=0;
            }
        }
        bool hold=gesture && choiceIsTarget && Owns(foreground) && (alt || releasedAt) && now-gestureAt<=5000;
        bool awaitingShell=gesture && neutral && releasedAt && now-releasedAt<=100;
        provisional=hold || awaitingShell;provisionalUntil=hold?gestureAt+5000:awaitingShell?releasedAt+100:0;
        altBefore=alt;tabBefore=tab;lastForeground=foreground;
    }
    void OnEvent(DWORD event,HWND window,DWORD time) {
        unsigned epoch=lossEpoch.load();ResetOwnerState();
        if(event==EVENT_SYSTEM_FOREGROUND) {
            bool ours=Owns(window);
            if(!ours && eventWasOurs)Lost(time);
            eventWasOurs=ours;
        } else if(event==EVENT_SYSTEM_SWITCHSTART) {
            DWORD now=Now();
            bool chord=!altExpired && api.key && (api.key(VK_MENU)&0x8000) && (api.key(VK_TAB)&0x8000) && RecentInput(now);
            if(chord)altChordAt=now;
            if(Owns(window) && chord) {switchPreviewAt=now;++switchStarts;} else switchPreviewAt=0;
        } else if(event==EVENT_SYSTEM_SWITCHEND) {
            DWORD chord=altChordAt.exchange(0),preview=switchPreviewAt.exchange(0),now=Now();
            if(Owns(window))++switchEnds;
            // SWITCHEND names the actual selected HWND. A switch to another app consumes the chord.
            if(Owns(window) && ((chord && now-chord<=1500) || (preview && now-preview<=5000)) && RecentInput(now,250))PublishSelection(2,time,epoch);
        }
    }
public:
    void Configure(Physical physical) { api=physical; }
    // Owner-thread only (CLI additionally serializes Pump/Refresh/SetTarget with its restore mutex).
    // The UI thread publishes mouse proof to its exact event HWND without retargeting this FSM.
    void SetTarget(HWND window,DWORD owner) {
        PublicationGuard guard(&publicationLock);
        if(target!=window || pid!=owner) {
            HWND old=target.load();DWORD oldOwner=0;GetWindowThreadProcessId(old,&oldOwner);
            if(old && oldOwner==pid.load()){RemovePropW(old,kActive);RemovePropW(old,kHeartbeat);RemovePropW(old,kProof);RemovePropW(old,kKind);}
            active=false;why=0;provisional=false;provisionalUntil=0;altChordAt=0;gainAt=0;switchPreviewAt=0;
            target=window;pid=owner;++generation;
        }
    }
    bool Owns(HWND window) const { return Matches(window,target.load(),pid.load()); }
    void InstallEvents() {
        listener=this;hookThread=GetCurrentThreadId();MSG message;PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
        switchHook=SetWinEventHook(EVENT_SYSTEM_SWITCHSTART,EVENT_SYSTEM_SWITCHEND,nullptr,Event,0,0,WINEVENT_OUTOFCONTEXT);
        foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,Event,0,0,WINEVENT_OUTOFCONTEXT);
    }
    void Pump() {
        if(GetCurrentThreadId()!=hookThread)return;
        MSG message;for(int n=0;n<64 && PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++n){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    void StopEvents() { if(switchHook)UnhookWinEvent(switchHook);if(foregroundHook)UnhookWinEvent(foregroundHook);switchHook=foregroundHook=nullptr;listener=nullptr; }
    void MouseActivate(HWND eventTarget,DWORD eventPid,LPARAM detail,HWND actualForeground) {
        UINT message=HIWORD(detail);
        bool press=message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN || message==WM_MBUTTONDOWN || message==WM_NCLBUTTONDOWN || message==WM_NCRBUTTONDOWN || message==WM_NCMBUTTONDOWN;
        DWORD now=Now();
        POINT point{};
        if(press && !Matches(actualForeground,eventTarget,eventPid) && Buttons() && RecentInput(now) && api.cursor && api.hit && api.cursor(&point) && Matches(api.hit(point),eventTarget,eventPid))PublishTo(eventTarget,eventPid,1,now);
    }
    void Lost(HWND expectedTarget,DWORD time) {
        PublicationGuard guard(&publicationLock);
        HWND h=target.load();DWORD proof=(DWORD)(ULONG_PTR)GetPropW(h,kProof);
        if(h!=expectedTarget)return;
        // A queued old loss must not erase a newer physical selection of this window.
        if(!proof || BeforeOrEqual(proof,time)) {
            ++lossEpoch;RemovePropW(h,kActive);RemovePropW(h,kHeartbeat);RemovePropW(h,kProof);RemovePropW(h,kKind);active=false;why=0;
        }
    }
    void Lost(DWORD time) {Lost(target.load(),time);}
    bool Refresh(HWND actualForeground) {
        unsigned epoch=lossEpoch.load();ResetOwnerState();unsigned identity=generation.load();
        bool ours=Owns(actualForeground);DWORD now=Now();unsigned buttons=Buttons();
        PhysicalSelection(actualForeground,now,epoch);
        if(sampled && (!ours || !pollWasOurs) && (buttons&~buttonsBefore) && RecentInput(now) && HitTarget())PublishSelection(1,now,epoch);
        buttonsBefore=buttons;sampled=true;
        if(!ours) { if(active || pollWasOurs || GetPropW(target.load(),kActive))Lost(now);pollWasOurs=false;return false; }
        if(!pollWasOurs)gainAt=now;
        pollWasOurs=true;
        // Serialize publication with the UI's loss handler so cached proof cannot resurrect
        // authorization after loss. This lock covers only own-window property operations.
        PublicationGuard guard(&publicationLock);
        HWND h=target.load();DWORD proof=(DWORD)(ULONG_PTR)GetPropW(h,kProof);DWORD owner=(DWORD)(ULONG_PTR)GetPropW(h,kActive);
        DWORD heartbeat=(DWORD)(ULONG_PTR)GetPropW(h,kHeartbeat);
        if(!active && proof && ((now-proof<=300) || (owner==pid.load() && heartbeat && now-heartbeat<=300))) {
            why=(int)(ULONG_PTR)GetPropW(h,kKind);active=true;++gains;SetPropW(h,kActive,(HANDLE)(ULONG_PTR)pid.load());
        }
        if(identity!=generation.load()){active=false;return false;}
        // A live observer may hand its selection to another detector after slow injection.
        // Orphaned publications expire in 300ms; the original proof remains distinct for loss ordering.
        if(active)SetPropW(h,kHeartbeat,(HANDLE)(ULONG_PTR)(now?now:1));
        return active;
    }
    bool EvaluatingAltTab() const {
        DWORD now=Now(),preview=switchPreviewAt.load(),chord=altChordAt.load();
        if(provisional && BeforeOrEqual(now,provisionalUntil.load()))return true;
        if(preview && now-preview<=5000 && api.key && (api.key(VK_MENU)&0x8000))return true;
        return chord && now-chord<=1500 && now-gainAt.load()<100;
    }
    bool Active() const { return active; }
    int Reason() const { return why; }
    long Count() const { return gains; }
    bool SwitchEventsAvailable() const { return switchHook!=nullptr; }
    long SwitchStarts() const {return switchStarts;}
    long SwitchEnds() const {return switchEnds;}
    bool Provisional() const {return provisional;}
};
}
