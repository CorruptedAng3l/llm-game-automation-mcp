// vinput_cli.cpp - vinput.exe / vinput32.exe: injector, launcher and control-pipe client.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>
#include <securityappcontainer.h>
#include <shobjidl.h>
#include <map>
#include <algorithm>
#include <shellapi.h>
#include <string>
#include <vector>
#include <set>
#include <cstdio>
#include <ctime>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <memory>
#include "vi_cli_deadline.h"
#include "vi_launch_exposure.h"
#include "vi_keys.h"
#include "vi_sessions.h"
#include "vi_text.h"
#include "vi_ncc.h"
#include "vi_ocr.h"
#include "vi_ocr_read.h"
#include "vi_image_encode.h"
#include "vi_motion.h"
#include "vi_paths.h"
#include "vi_user_intent.h"
#define VI_ACGUARD_SCANNER
#include "vi_acguard.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
static bool g_jsonOutput = false;
#pragma comment(lib, "ole32.lib")

static std::string W2U(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
static std::wstring U2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w;
}
static std::string JEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; } else o += (char)c; }
    return o;
}
static std::wstring ExeDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    wchar_t* s = wcsrchr(p, L'\\'); if (s) *s = 0; return p;
}
static bool IsNumber(const std::string& s) {
    if (s.empty()) return false; size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0; if (i >= s.size()) return false;
    bool dot = false; for (; i < s.size(); i++) { if (s[i] == '.' && !dot) { dot = true; continue; } if (!isdigit((unsigned char)s[i])) return false; } return true;
}

// ------------------------------------------------------------------------------------------
// pipe client
// ------------------------------------------------------------------------------------------
// AppContainer targets (UWP / Xbox app games) create their pipe as \\.\pipe\LOCAL\vinput_<pid>; the system places
// it in the container's namespace: \\.\pipe\Sessions\<n>\AppContainerNamedObjects\<container SID>\vinput_<pid>.
struct TargetToken { bool known = false, appContainer = false; DWORD session = 0; std::wstring acPath; };
static TargetToken QueryTargetToken(DWORD pid) {
    TargetToken t;
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!hp) return t;
    HANDLE tok = nullptr;
    if (OpenProcessToken(hp, TOKEN_QUERY, &tok)) {
        DWORD isAc = 0, n = 0;
        t.known = true;
        if (GetTokenInformation(tok, TokenIsAppContainer, &isAc, sizeof(isAc), &n) && isAc) {
            t.appContainer = true;
            GetTokenInformation(tok, TokenSessionId, &t.session, sizeof(t.session), &n);
            wchar_t path[512]; ULONG len = 0;
            if (GetAppContainerNamedObjectPath(tok, nullptr, 512, path, &len)) t.acPath = path;
        }
        CloseHandle(tok);
    }
    CloseHandle(hp);
    return t;
}
static std::wstring PipeName(DWORD pid) {
    static std::map<DWORD, std::wstring> cache;
    static std::mutex cacheMx;
    std::lock_guard<std::mutex> lk(cacheMx);
    auto it = cache.find(pid); if (it != cache.end()) return it->second;
    wchar_t name[700]; swprintf_s(name, L"\\\\.\\pipe\\vinput_%lu", pid);
    TargetToken t = QueryTargetToken(pid);
    if (t.appContainer && !t.acPath.empty()) swprintf_s(name, L"\\\\.\\pipe\\Sessions\\%lu\\%s\\vinput_%lu", t.session, t.acPath.c_str(), pid);
    if (t.known) cache[pid] = name;
    return name;
}
// Grants the AppContainer groups (ALL APPLICATION PACKAGES S-1-15-2-1, ALL RESTRICTED APPLICATION PACKAGES
// S-1-15-2-2) access to a file or directory, merged into its existing DACL.
static bool GrantAppContainers(const std::wstring& path, DWORD access, bool inherit, std::string& err) {
    PACL oldDacl = nullptr, newDacl = nullptr; PSECURITY_DESCRIPTOR sd = nullptr;
    DWORD r = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl, nullptr, &sd);
    if (r != ERROR_SUCCESS) { err = "GetNamedSecurityInfo " + std::to_string(r); return false; }
    PSID sids[2] = {}; ConvertStringSidToSidW(L"S-1-15-2-1", &sids[0]); ConvertStringSidToSidW(L"S-1-15-2-2", &sids[1]);
    EXPLICIT_ACCESSW ea[2] = {};
    for (int i = 0; i < 2; i++) {
        ea[i].grfAccessPermissions = access; ea[i].grfAccessMode = GRANT_ACCESS;
        ea[i].grfInheritance = inherit ? (SUB_CONTAINERS_AND_OBJECTS_INHERIT) : NO_INHERITANCE;
        ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID; ea[i].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP; ea[i].Trustee.ptstrName = (LPWSTR)sids[i];
    }
    r = SetEntriesInAclW(2, ea, oldDacl, &newDacl);
    if (r == ERROR_SUCCESS) r = SetNamedSecurityInfoW((LPWSTR)path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr);
    if (r != ERROR_SUCCESS) err = "SetNamedSecurityInfo " + std::to_string(r);
    if (newDacl) LocalFree(newDacl); if (sd) LocalFree(sd); LocalFree(sids[0]); LocalFree(sids[1]);
    return r == ERROR_SUCCESS;
}
static thread_local ULONGLONG g_cliDeadline=0;
static DWORD CliBudget(DWORD cap){return vi_cli_deadline::Remaining(g_cliDeadline,GetTickCount64(),cap);}
struct CliDeadlineScope {ULONGLONG prior;explicit CliDeadlineScope(DWORD budget):prior(g_cliDeadline){const auto next=vi_cli_deadline::End(GetTickCount64(),budget);g_cliDeadline=prior?(std::min)(prior,next):next;}~CliDeadlineScope(){g_cliDeadline=prior;}};
static HANDLE OpenPipe(DWORD pid, DWORD timeoutMs, HANDLE targetProcess = nullptr) {
    std::wstring nameS = PipeName(pid); const wchar_t* name = nameS.c_str();
    const auto deadline=vi_cli_deadline::End(GetTickCount64(),CliBudget(timeoutMs));
    while(CliBudget(vi_cli_deadline::Remaining(deadline,GetTickCount64(),MAXDWORD))) {
        if (targetProcess && WaitForSingleObject(targetProcess, 0) == WAIT_OBJECT_0) return INVALID_HANDLE_VALUE;
        HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
        DWORD e = GetLastError();
        const auto remaining=CliBudget(vi_cli_deadline::Remaining(deadline,GetTickCount64(),e==ERROR_PIPE_BUSY?500:100));
        if(!remaining)break;if (e == ERROR_PIPE_BUSY) WaitNamedPipeW(name,remaining);else Sleep(remaining);
    }
    return INVALID_HANDLE_VALUE;
}
static bool PipeRequest(HANDLE h, const std::string& line, std::string& resp) {
    std::string l = line + "\n"; DWORD w;
    if (!WriteFile(h, l.data(), (DWORD)l.size(), &w, nullptr)) return false;
    resp.clear(); char c[4096];
    for (;;) {
        DWORD n = 0; if (!ReadFile(h, c, sizeof(c), &n, nullptr) || !n) return false;
        resp.append(c, n);
        size_t nl = resp.find('\n'); if (nl != std::string::npos) { resp.resize(nl); return true; }
    }
}
// Ownership: `--owner NAME` (or env VINPUT_OWNER) is stamped into every request this CLI sends, so a target
// claimed by another agent refuses it. `--force-claim` overrides a foreign claim (logged in the target).
static std::string g_owner;
static bool g_forceClaim = false;
static std::string StampOwner(const std::string& line) {
    if ((g_owner.empty() && !g_forceClaim) || line.empty() || line[0] != '{' || line.find("\"owner\"") != std::string::npos) return line;
    std::string add;
    if (!g_owner.empty()) add = "\"owner\":\"" + JEsc(g_owner) + "\"";
    if (g_forceClaim) add += std::string(add.empty() ? "" : ",") + "\"force_claim\":true";
    size_t body = line.find_first_not_of(" \t", 1);
    bool emptyObject = body != std::string::npos && line[body] == '}';
    return "{" + add + (emptyObject ? "" : ",") + line.substr(1);
}
#include "vi_cli_transport.inl"
// Reviewed c63ab25: discover hidden in-process wrapper presentation windows from the CLI.
namespace vi_doctor{static std::string Field(const std::string&,const std::string&);}
struct OverlayFind{DWORD pid;HWND best;unsigned long long area;};
static BOOL CALLBACK OverlayFindProc(HWND window,LPARAM value){auto* search=(OverlayFind*)value;DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=search->pid||!IsWindowVisible(window))return TRUE;wchar_t name[96]{};if(!GetClassNameW(window,name,96)||!wcsstr(name,L"PresentationWindow"))return TRUE;RECT r{};if(!GetClientRect(window,&r)||r.right<=0||r.bottom<=0)return TRUE;auto area=(unsigned long long)r.right*r.bottom;if(area>search->area){search->best=window;search->area=area;}return TRUE;}
static std::string StampOverlay(DWORD pid,const std::string& line){auto cmd=vi_doctor::Field(line,"cmd");if((cmd!="\"capture\""&&cmd!="\"frame-hash\"")||vi_doctor::Field(line,"overlay")!="null")return line;auto start=line.find_first_not_of(" \t\r\n");if(start==std::string::npos||line[start]!='{')return line;OverlayFind search{pid,nullptr,0};EnumWindows(OverlayFindProc,(LPARAM)&search);if(!search.best)return line;return line.substr(0,start+1)+"\"overlay\":"+std::to_string((unsigned long long)(ULONG_PTR)search.best)+","+line.substr(start+1);}
static bool CaptureScopePreflight(DWORD,std::string&,std::string&);
static bool Request(DWORD pid, const std::string& rawLine, std::string& resp, DWORD connectTimeout = 5000, HANDLE targetProcess = nullptr) {
    std::string admitted=rawLine;
    if(CaptureScopePreflight(pid,admitted,resp))return true;
    if(g_cliDeadline){const bool received=BoundedCliRequest(pid,StampOverlay(pid,admitted),CliBudget(connectTimeout),resp,targetProcess);if(!received&&resp.empty())resp="{\"ok\":false,\"error\":\"launch pipe transport/deadline failed\"}";return received;}
    std::string line = StampOverlay(pid,StampOwner(admitted));
    HANDLE h = OpenPipe(pid, connectTimeout, targetProcess);
    if (h == INVALID_HANDLE_VALUE) { resp = "{\"ok\":false,\"error\":\"cannot connect to \\\\\\\\.\\\\pipe\\\\vinput_" + std::to_string(pid) + " (not injected / process gone)\"}"; return false; }
    bool ok = PipeRequest(h, line, resp);
    CloseHandle(h);
    if (!ok) resp = "{\"ok\":false,\"error\":\"pipe i/o failed\"}";
    return ok;
}

// ------------------------------------------------------------------------------------------
// CLI-args -> JSON command
// ------------------------------------------------------------------------------------------
struct Args { std::vector<std::string> pos; std::vector<std::pair<std::string, std::string>> opt; };
static bool HasOpt(const Args& a, const char* k);
static Args SplitArgs(const std::vector<std::string>& a,bool burstQuiet=false) {
    Args r;
    for (size_t i = 0; i < a.size(); i++) {
        const std::string& s = a[i];
        if (s.size() > 2 && s[0] == '-' && s[1] == '-') {
            std::string k = s.substr(2), v = "true";
            size_t eq = k.find('='); if (eq != std::string::npos) { v = k.substr(eq + 1); k = k.substr(0, eq); }
            else if(burstQuiet&&k=="quiet") { // Only consume an explicit boolean, never the sheet path.
                if(i+1<a.size()&&(a[i+1]=="true"||a[i+1]=="false"))v=a[++i];
            }
            else if (i + 1 < a.size() && !(a[i + 1].size() > 2 && a[i + 1][0] == '-' && a[i + 1][1] == '-') &&
                     k != "placement" && k != "async" && k != "replace" && k != "strict-frames" && k != "norm" && k != "keys" && k != "hold" && k != "horizontal" && k != "capture-space" && k != "raw" && k != "force" && k != "json" && k != "exact" && k != "gone" && k != "absent" && k != "dry-run" && k != "continue" && k != "lines-only" && k != "no-scale" && k != "tiles" && k != "no-tiles" && k != "optional" && k != "allow-dialog" && k != "hooks-only") { v = a[++i]; }
            r.opt.push_back({k, v});
        } else r.pos.push_back(s);
    }
    return r;
}
static void Put(std::string& j, const std::string& k, const std::string& v) {
    if (j.size() > 1) j += ',';
    j += "\"" + JEsc(k) + "\":";
    if (IsNumber(v) || v == "true" || v == "false") j += v; else j += "\"" + JEsc(v) + "\"";
}
static void PutS(std::string& j, const std::string& k, const std::string& v) { if (j.size() > 1) j += ','; j += "\"" + JEsc(k) + "\":\"" + JEsc(v) + "\""; }

static bool BuildJson(const std::vector<std::string>& argv, std::string& out, std::string& err) {
    if (argv.empty()) { err = "no command"; return false; }
    std::string cmd = argv[0];
    if (cmd == "json") { if (argv.size() < 2) { err = "json needs a payload"; return false; } out = argv[1]; return true; }
    Args a = SplitArgs(std::vector<std::string>(argv.begin() + 1, argv.end()),cmd=="capture-burst");
    if(HasOpt(a,"stagger-ms")&&cmd!="pad-timeline"){err="--stagger-ms is only implemented for pad-timeline; no input was sent";return false;}
    if((HasOpt(a,"for")||HasOpt(a,"for-ms")||HasOpt(a,"for_ms"))&&cmd!="config"){err="--for applies only to config isolate=false";return false;}
    std::string j = "{"; PutS(j, "cmd", cmd);
    auto P = [&](size_t i) -> std::string { return i < a.pos.size() ? a.pos[i] : std::string(); };
    if (cmd == "key" || cmd == "down" || cmd == "up" || cmd == "pulse") { if (a.pos.empty()) { err = cmd + " needs a key"; return false; } PutS(j, "key", P(0)); if (!P(1).empty()) Put(j, "ms", P(1)); }
    else if (cmd == "hold") { if (a.pos.empty()) { err = "hold needs a key"; return false; } PutS(j, "key", P(0)); Put(j, "ms", P(1).empty() ? "1000" : P(1)); }
    else if(cmd=="readiness") {
        if(!a.pos.empty()){err="readiness takes no positional arguments; use --wait-ms N and --hooks-only=true|false";return false;}
        std::set<std::string> seen;
        for(const auto& option:a.opt) {
            if(option.first!="wait-ms"&&option.first!="hooks-only"){err="readiness accepts only --wait-ms and --hooks-only";return false;}
            if(!seen.insert(option.first).second){err="duplicate readiness option: --"+option.first;return false;}
            if(option.first=="hooks-only"&&option.second!="true"&&option.second!="false"){err="--hooks-only must be true or false";return false;}
        }
    }
    else if(cmd=="job-status"||cmd=="job-cancel"){if(P(0).empty()){err=cmd+" needs a job ID";return false;}PutS(j,"id",P(0));}
    else if(cmd=="pad-ramp"){if(a.pos.size()!=4){err="pad-ramp <axis> <from> <to> <ms>";return false;}PutS(j,"axis",P(0));Put(j,"from",P(1));Put(j,"to",P(2));Put(j,"ms",P(3));}
    else if(cmd=="pad-timeline"){if(a.pos.size()!=1){err="pad-timeline needs a JSON state-array file";return false;}FILE* file=nullptr;_wfopen_s(&file,U2W(P(0)).c_str(),L"rb");if(!file){err="cannot open timeline file";return false;}std::string content;char block[4096];size_t size;bool valid=true;while((size=fread(block,1,sizeof(block),file))){if(content.size()+size>65536){valid=false;break;}content.append(block,size);}valid=valid&&!ferror(file)&&feof(file);fclose(file);if(!valid){err="timeline exceeds64KiB or could not be read";return false;}PutS(j,"states_json",content);}
    else if (cmd == "trace") { if (P(0) != "key" || P(1).empty()) { err = "trace key <vk> required"; return false; } PutS(j, "key", P(1)); }
    else if (cmd == "type") { PutS(j, "text", P(0)); }
    else if (cmd == "mouse") { if (a.pos.size() < 2) { err = "mouse needs x y"; return false; } Put(j, "x", P(0)); Put(j, "y", P(1)); }
    else if (cmd == "move") { if (a.pos.size() < 2) { err = "move needs dx dy"; return false; } Put(j, "dx", P(0)); Put(j, "dy", P(1)); }
    else if (cmd == "click" || cmd == "mousedown" || cmd == "mouseup") {
        size_t i = 0;
        if (!a.pos.empty() && !IsNumber(a.pos[0])) { PutS(j, "button", a.pos[0]); i = 1; }
        if (a.pos.size() >= i + 2) { Put(j, "x", a.pos[i]); Put(j, "y", a.pos[i + 1]); }
    }
    else if (cmd == "wheel") Put(j, "delta", P(0).empty() ? "120" : P(0));
    else if (cmd == "pad") {
        if (a.pos.size() > 2) { err = "pad accepts one button list and one duration; no input was injected"; return false; }
        std::set<std::string> padOptions;
        for (const auto& option : a.opt) {
            if (!padOptions.insert(option.first).second) { err = "duplicate pad option --" + option.first + "; no input was injected"; return false; }
            const std::string& name = option.first;
            if (name != "ms" && name != "hold" && name != "async" && name != "replace" && name != "lt" && name != "rt" && name != "lx" && name != "ly" && name != "rx" && name != "ry" && name != "buttons") { err = "unknown pad option --" + name + "; no input was injected"; return false; }
        }
        if (padOptions.count("buttons")) { err = "pad takes button names positionally, e.g. pad a+lt 100; use the buttons field in direct JSON"; return false; }
        bool hold = false, hasDuration = false;
        for (const auto& o : a.opt) {
            if (o.first == "hold") {
                if (o.second != "true" && o.second != "false") { err = "pad --hold must be true or false (bare --hold is indefinite); omit it for a duration"; return false; }
                hold = o.second == "true";
            }
            if (o.first == "ms") hasDuration = true;
        }
        if (hold && (hasDuration || a.pos.size() > 1 || (a.pos.size() == 1 && IsNumber(P(0))))) {
            err = "pad --hold is indefinite and cannot take a duration; omit --hold for a timed pad command, e.g. pad --lx 30000 1500";
            return false;
        }
        if (hasDuration && (a.pos.size() > 1 || (a.pos.size() == 1 && IsNumber(P(0))))) { err = "pad duration specified more than once; no input was injected"; return false; }
        // "pad --lx 30000 1500" used to send buttons="1500" and the default 100 ms: a lone number is the duration
        if (a.pos.size() == 1 && IsNumber(P(0))) Put(j, "ms", P(0));
        else { PutS(j, "buttons", P(0)); if (!P(1).empty()) Put(j, "ms", P(1)); }
    }
    else if (cmd == "capture") { if (a.pos.empty()) { err = "capture needs a path"; return false; } wchar_t full[MAX_PATH]; GetFullPathNameW(U2W(P(0)).c_str(), MAX_PATH, full, nullptr); PutS(j, "path", W2U(full)); }
    else if (cmd == "wait") Put(j, "ms", P(0).empty() ? "100" : P(0));
    else if(cmd=="config"){
        std::set<std::string> seen;
        for(const auto& positional:a.pos){const auto eq=positional.find('=');if(eq==std::string::npos){err="config expects name=value";return false;}const auto name=positional.substr(0,eq);if(!seen.insert(name).second){err="duplicate config field "+name;return false;}Put(j,name,positional.substr(eq+1));}
        for(const auto& option:a.opt){const auto name=option.first=="for"||option.first=="for-ms"?"for_ms":option.first;if(!seen.insert(name).second){err="duplicate config field "+name;return false;}}
    }
    for (auto& o : a.opt) {
        std::string k = o.first;
        if (k == "capture-space") { PutS(j, "space", "capture"); continue; }
        if (k == "scope-json") { PutS(j,"expected_capture_scope",o.second); continue; }
        if (k == "step-ms") k = "step_ms"; if (k == "settle-ms") k = "settle_ms"; if (k == "timeout-ms") k = "timeout_ms";
        if (k == "hover-frames") k = "hover_frames"; if (k == "hover-timeout-ms") k = "hover_timeout_ms";
        if (k == "di-poll-timeout-ms") k = "di_poll_timeout_ms";
        if (k == "di-release-timeout-ms") k = "di_release_timeout_ms";
        if(k=="period-ms")k="period_ms";if(k=="duration-ms")k="ms";if(k=="end-hold-ms")k="end_hold_ms";if(k=="stagger-ms")k="stagger_ms";if(k=="strict-frames")k="strict_frames";
        if((k=="for"||k=="for-ms")&&cmd=="config")k="for_ms";
        if (k == "wait-ms") k = "wait_ms";if (k == "hooks-only") k = "hooks_only";
        if (k == "after-frames") k = "after_frames";
        if (k == "max-width") k = "max_width";
        Put(j, k, o.second);
    }
    out = j + "}";
    return true;
}

static std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> v; std::string cur; bool q = false, any = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (q) { if (c == '"') q = false; else if (c == '\\' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; } else cur += c; }
        else if (c == '"') { q = true; any = true; }
        else if (c == ' ' || c == '\t') { if (!cur.empty() || any) { v.push_back(cur); cur.clear(); any = false; } }
        else cur += c;
    }
    if (!cur.empty() || any) v.push_back(cur);
    return v;
}

// ------------------------------------------------------------------------------------------
// process helpers
// ------------------------------------------------------------------------------------------
static bool ProcessIs32(DWORD pid, bool& is32) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    BOOL wow = FALSE; IsWow64Process(h, &wow); CloseHandle(h);
    is32 = wow != FALSE;
#ifndef _WIN64
    BOOL selfwow = FALSE; IsWow64Process(GetCurrentProcess(), &selfwow); if (!selfwow) is32 = true;  // 32-bit OS
#endif
    return true;
}
static std::wstring ProcName(DWORD pid) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32W e = {sizeof(e)}; std::wstring r;
    if (Process32FirstW(s, &e)) do { if (e.th32ProcessID == pid) { r = e.szExeFile; break; } } while (Process32NextW(s, &e));
    CloseHandle(s); return r;
}
static std::set<DWORD> PidsByName(const std::wstring& name) {
    std::set<DWORD> r; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32W e = {sizeof(e)};
    if (Process32FirstW(s, &e)) do { if (!_wcsicmp(e.szExeFile, name.c_str())) r.insert(e.th32ProcessID); } while (Process32NextW(s, &e));
    CloseHandle(s); return r;
}
struct WinFind { DWORD pid; HWND h; HWND dialog = nullptr, ownedPopup = nullptr; long long area = 0; bool enabled = false; };
static bool OwnedPopupWindow(HWND window) {
    return window && GetWindow(window,GW_OWNER)!=nullptr && ((DWORD)GetWindowLongW(window,GWL_STYLE)&WS_POPUP)!=0;
}
static BOOL CALLBACK WinEnum(HWND h, LPARAM lp) {
    WinFind* f = (WinFind*)lp; DWORD p=0; GetWindowThreadProcessId(h, &p);
    if (p == f->pid && IsWindowVisible(h)) {
        wchar_t cls[128]={0};GetClassNameW(h,cls,127);
        if(!wcscmp(cls,L"#32770")){if(!f->dialog)f->dialog=h;return TRUE;}
        if(OwnedPopupWindow(h)){if(!f->ownedPopup)f->ownedPopup=h;return TRUE;}
        // Main candidates mirror the core's unowned-window policy. An unowned
        // WS_POPUP is a valid borderless game window, regardless of its title.
        if(GetWindow(h,GW_OWNER)||((DWORD)GetWindowLongW(h,GWL_EXSTYLE)&WS_EX_TOOLWINDOW))return TRUE;
        RECT r{};if(GetClientRect(h,&r)&&r.right>=200&&r.bottom>=150) {
            long long area=(long long)r.right*r.bottom;
            const bool enabled=IsWindowEnabled(h)!=FALSE;
            if(!f->h||(enabled&&!f->enabled)||(enabled==f->enabled&&area>f->area)){f->h=h;f->area=area;f->enabled=enabled;}
        }
    }
    // Do not stop on the first visible popup or main: an enabled real main
    // wins even when an owned popup is above it in the enumeration order.
    return TRUE;
}
// UWP: the game's CoreWindow is a child of ApplicationFrameHost's ApplicationFrameWindow (another process).
static BOOL CALLBACK CoreChildEnum(HWND h, LPARAM lp) {
    WinFind* f = (WinFind*)lp; DWORD p = 0; GetWindowThreadProcessId(h, &p);
    wchar_t cls[64] = {0}; GetClassNameW(h, cls, 63);
    if (p == f->pid && IsWindowVisible(h) && !wcscmp(cls, L"Windows.UI.Core.CoreWindow")) { f->h = h; return FALSE; }
    return TRUE;
}
static BOOL CALLBACK FrameEnum(HWND h, LPARAM lp) {
    wchar_t cls[64] = {0}; GetClassNameW(h, cls, 63);
    if (!wcscmp(cls, L"ApplicationFrameWindow")) { EnumChildWindows(h, CoreChildEnum, lp); if (((WinFind*)lp)->h) return FALSE; }
    return TRUE;
}
static HWND CoreWindowOf(DWORD pid) { WinFind f{pid, nullptr}; EnumWindows(FrameEnum, (LPARAM)&f); return f.h; }
static HWND VisibleWindowOf(DWORD pid) {
    WinFind f{pid,nullptr};EnumWindows(WinEnum,(LPARAM)&f);
    HWND main=f.h?f.h:CoreWindowOf(pid),dialog=f.dialog?f.dialog:f.ownedPopup;
    if(dialog&&(!main||!IsWindowEnabled(main)))return dialog;
    return main?main:dialog;
}
static std::string WindowClass(HWND h) { wchar_t s[128]={0};GetClassNameW(h,s,127);return W2U(s); }
static std::string WindowTitle(HWND h) {
    DWORD owner=0;if(!GetWindowThreadProcessId(h,&owner)||owner==GetCurrentProcessId())return {};
    wchar_t text[512]={0};GetWindowTextW(h,text,512);return W2U(text);
}
struct DialogText { std::wstring text; unsigned controls=0; DWORD pid=0; };
static BOOL CALLBACK DialogTextEnum(HWND h,LPARAM param) {
    auto* d=(DialogText*)param;if(++d->controls>16 || d->text.size()>=4096)return FALSE;
    DWORD owner=0;if(!GetWindowThreadProcessId(h,&owner)||owner==GetCurrentProcessId()||owner!=d->pid)return TRUE;
    wchar_t s[512]={0};DWORD_PTR chars=0;
    if(SendMessageTimeoutW(h,WM_GETTEXT,512,(LPARAM)s,SMTO_ABORTIFHUNG|SMTO_BLOCK,75,&chars) && s[0]) {
        if(!d->text.empty())d->text.append(L" | ",(std::min)(size_t(3),size_t(4096)-d->text.size()));
        d->text.append(s,(std::min)(wcslen(s),size_t(4096)-d->text.size()));
    }
    return TRUE;
}
static std::string WindowFields(HWND h) { char value[32];sprintf_s(value,"0x%llX",(unsigned long long)(ULONG_PTR)h);return ",\"window_hwnd\":\""+std::string(value)+"\",\"window_class\":\""+JEsc(WindowClass(h))+"\",\"window_title\":\""+JEsc(WindowTitle(h))+"\""; }
static std::string LaunchDialogFields(HWND window) {
    char owner[32];sprintf_s(owner,"0x%llX",(unsigned long long)(ULONG_PTR)GetWindow(window,GW_OWNER));
    return WindowFields(window)+",\"dialog_owned_popup\":"+(OwnedPopupWindow(window)?"true":"false")+",\"dialog_owner_hwnd\":\""+owner+"\"";
}
#include "vi_launch_splash.h"
static bool PendingStartupSplash(HWND window,DWORD pid){
 DWORD actual=0,owner=0;if(!window||!GetWindowThreadProcessId(window,&actual)||actual!=pid)return false;
 const HWND parent=GetWindow(window,GW_OWNER);if(parent)GetWindowThreadProcessId(parent,&owner);
 DialogText text;text.pid=pid;EnumChildWindows(window,DialogTextEnum,(LPARAM)&text);
 const auto style=(DWORD)GetWindowLongW(window,GWL_STYLE);
 return vi_launch_splash::Pending(OwnedPopupWindow(window),owner==pid,(style&(WS_CAPTION|WS_THICKFRAME))==0,WindowClass(window)=="#32770",text.text.empty());
}
// The process that owns the foreground "for" a target: the target itself, or ApplicationFrameHost hosting its CoreWindow.
static bool ForegroundIsTarget(HWND fg, DWORD target) {
    if (!fg || !target) return false;
    DWORD p = 0; GetWindowThreadProcessId(fg, &p); if (p == target) return true;
    HWND core = CoreWindowOf(target);
    return core && GetAncestor(core, GA_ROOT) == fg;
}

#include "vi_cli_inject_reply.inl"
static int RunChild32(const std::vector<std::wstring>& args,bool quiet=false,std::string* captured=nullptr) {
    FILE* output=quiet?stderr:stdout;std::string directRecord;const bool direct=captured==nullptr;
    if(!captured)captured=&directRecord;
    StandaloneInjectChildReply directReply{output,&directRecord,direct,quiet};captured->clear();
    if(!CliBudget(1)){EmitInjectReply(output,captured,"{\"ok\":false,\"injected\":null,\"error\":\"launch deadline expired before delegated injection; no child injector was started\"}");return 2;}
    std::wstring exe=ExeDir()+L"\\vinput32.exe";
    if(GetFileAttributesW(exe.c_str())==INVALID_FILE_ATTRIBUTES){EmitInjectReply(output,captured,"{\"ok\":false,\"error\":\"32-bit target needs vinput32.exe next to vinput.exe\"}");return 2;}
    std::wstring command=L"\""+exe+L"\"";
    for(const auto& argument:args)command+=L" \""+argument+L"\"";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
    InjectChildHandle readPipe,writePipe,childError,childInput;
    if(captured) {
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE reader=nullptr,writer=nullptr;
        if(!CreatePipe(&reader,&writer,&security,65536)){EmitInjectReply(output,captured,"{\"ok\":false,\"error\":\"cannot create delegated injector stdout pipe\",\"win32_error\":%lu}",GetLastError());return 2;}
        readPipe.Reset(reader);writePipe.Reset(writer);
        if(!SetHandleInformation(reader,HANDLE_FLAG_INHERIT,0)||!CopyInjectChildStandard(STD_ERROR_HANDLE,true,childError)||!CopyInjectChildStandard(STD_INPUT_HANDLE,false,childInput)) {
            EmitInjectReply(output,captured,"{\"ok\":false,\"error\":\"cannot prepare delegated injector stdio handles\",\"win32_error\":%lu}",GetLastError());return 2;
        }
        startup.dwFlags|=STARTF_USESTDHANDLES;startup.hStdOutput=writer;startup.hStdError=childError.value;startup.hStdInput=childInput.value;
    }
    if(!CliBudget(1)){EmitInjectReply(output,captured,"{\"ok\":false,\"injected\":null,\"error\":\"launch deadline expired while preparing delegated injection; no child injector was started\"}");return 2;}
    if(!CreateProcessW(exe.c_str(),&command[0],nullptr,nullptr,TRUE,captured?CREATE_NO_WINDOW:0,nullptr,nullptr,&startup,&child)) {
        EmitInjectReply(output,captured,"{\"ok\":false,\"error\":\"cannot start vinput32.exe\"}");return 2;
    }
    InjectChildHandle process,thread;process.Reset(child.hProcess);thread.Reset(child.hThread);
    writePipe.Reset();childError.Reset();childInput.Reset();

    std::string reply;bool overflow=false,readFailed=false,exited=false,stdoutEof=false;
    const DWORD delegatedBudget=CliBudget(60000);const ULONGLONG deadline=vi_cli_deadline::End(GetTickCount64(),delegatedBudget);
    while(GetTickCount64()<deadline) {
        DWORD available=0;
        if(!PeekNamedPipe(readPipe.value,nullptr,0,nullptr,&available,nullptr)) {
            if(GetLastError()!=ERROR_BROKEN_PIPE){readFailed=true;break;}
            stdoutEof=true;available=0;
        }
        if(available) {
            char block[4096];DWORD read=0;
            if(!ReadFile(readPipe.value,block,(std::min)(available,(DWORD)sizeof(block)),&read,nullptr)||!read){readFailed=true;break;}
            if(reply.size()+read<=65536&&!overflow)reply.append(block,read);else overflow=true;
            continue; // Drain already-available bytes without blocking the GUI watcher.
        }
        exited=WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;
        // A signalled process can leave final bytes in its pipe, or another
        // inherited writer can keep it open. Only observed EOF proves draining;
        // peek again on the next bounded iteration, never block on ReadFile.
        if(exited&&stdoutEof)break;
        Sleep(vi_cli_deadline::Remaining(deadline,GetTickCount64(),2));
    }
    exited=WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;
    if(!exited) {
        // This is the original retained handle from our own CreateProcess, never a reopened PID.
        const bool terminated=TerminateProcess(process.value,2)!=FALSE;
        const bool reaped=WaitForSingleObject(process.value,1500)==WAIT_OBJECT_0;
        EmitInjectReply(output,captured,"{\"ok\":false,\"reply_unavailable\":true,\"injected\":null,\"delegated_deadline_ms\":%lu,\"child_terminated\":%s,\"child_reaped\":%s,\"error\":\"delegated injector reply/deadline failed; target injection state is unknown and was not retried\"}",delegatedBudget,terminated?"true":"false",reaped?"true":"false");return 2;
    }
    DWORD code=1;const bool codeKnown=GetExitCodeProcess(process.value,&code)!=FALSE;
    const auto first=reply.find_first_not_of(" \t\r\n");if(first!=std::string::npos)reply.erase(0,first);
    while(!reply.empty()&&isspace((unsigned char)reply.back()))reply.pop_back();
    vi_ready_json::Reader parsed{reply,0,{}};bool ok=false;
    if(!codeKnown||!stdoutEof||overflow||readFailed||reply.find_first_of("\r\n")!=std::string::npos||!parsed.Parse()||!parsed.Bool("ok",ok)||ok!=(code==0)) {
        EmitInjectReply(output,captured,"{\"ok\":false,\"reply_unavailable\":true,\"injected\":null,\"child_exit_code\":%lu,\"error\":\"delegated injector stdout did not contain one bounded JSON result consistent with its exit code; injection was not retried\"}",code);return 2;
    }
    *captured=std::move(reply);return (int)code;
}

static bool PrintInjectionExit(FILE* output, HANDLE target, DWORD pid, const char* stage, DWORD observeMs = 0, std::string* captured = nullptr) {
    DWORD code = 0;
    // Wait on the original handle: STILL_ACTIVE (259) is also a legal process exit code,
    // and reopening the numeric PID here could describe a different reused process.
    if (!target || WaitForSingleObject(target, observeMs) != WAIT_OBJECT_0 || !GetExitCodeProcess(target, &code)) return false;
    EmitInjectReply(output,captured, "{\"ok\":false,\"pid\":%lu,\"target_exited\":true,\"exit_code\":%lu,\"stage\":\"%s\",\"error\":\"target exited (pid %lu, exit code %lu) during inject\"}\n", pid, code, stage, pid, code);
    return true;
}
// Anti-cheat refusal (vi_acguard.h). There is deliberately no flag to skip it.
static bool g_acSuspended = false;      // set by launch --early: the target is our own suspended child
static std::string g_acLastRefusal;     // JSON object of the last refusal, for launch's reply
static std::string AcRefusalJson(const vi_ac::Finding& f, DWORD pid) {
    std::string what = f.kind == "unverifiable" ? f.error : (f.product + " " + f.kind + " '" + f.name + "'");
    std::string j = "{\"ok\":false,\"anticheat_refused\":true";
    if (pid) j += ",\"pid\":" + std::to_string(pid);
    j += ",\"error\":\"" + JEsc("refused: anti-cheat detected (" + what + "). vinput is for single-player / offline games only and never "
         "injects into a game protected by anti-cheat or while an anti-cheat is running. This check has no override.") + "\"";
    j += ",\"anticheat\":{\"product\":\"" + JEsc(f.product) + "\",\"kind\":\"" + JEsc(f.kind) + "\",\"name\":\"" + JEsc(f.name) + "\"}}";
    return j;
}
static DWORD g_cliReadinessWaitMs=5000;
#include "vi_cli_readiness.inl"
static int Inject(DWORD pid, std::wstring dll, bool quiet = false, bool hooksOnly = false, std::string* captured = nullptr) {
    FILE* output = quiet ? stderr : stdout;
    {
        vi_ac::Finding f = vi_ac::CheckTarget(pid, g_acSuspended);
        if (f.refused) { g_acLastRefusal = AcRefusalJson(f, pid); fprintf(output, "%s\n", g_acLastRefusal.c_str()); return 4; }
    }
    if(!CliBudget(1)){EmitInjectReply(output,captured,"{\"ok\":false,\"pid\":%lu,\"injected\":null,\"error\":\"launch deadline expired before injection; no new load was requested\"}",pid);return 2;}
    bool is32 = false;
    if (!ProcessIs32(pid, is32)) { EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"cannot open pid %lu\"}\n", pid); return 2; }
#ifdef _WIN64
    if (is32) { std::vector<std::wstring> a = {L"inject", L"--pid", std::to_wstring(pid)}; if (g_acSuspended) a.push_back(L"--internal-suspended"); if(hooksOnly)a.push_back(L"--hooks-only");else if(g_cliReadinessWaitMs!=5000){a.push_back(L"--readiness-wait-ms");a.push_back(std::to_wstring(g_cliReadinessWaitMs));} if (!dll.empty()) { a.push_back(L"--dll"); a.push_back(dll); } return RunChild32(a,quiet,captured); }
    if (dll.empty()) dll = ExeDir() + L"\\vinput64.dll";
#else
    if (!is32) { EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"64-bit target: use vinput.exe\"}\n"); return 2; }
    if (dll.empty()) dll = ExeDir() + L"\\vinput32.dll";
#endif
    std::string resp;
    if (ReadinessTransport(pid, "{\"cmd\":\"ping\"}", 200,resp)) {
        HANDLE existing=OpenProcess(SYNCHRONIZE,FALSE,pid);
        int result=ReportInputReady(output,pid,dll,quiet,hooksOnly,true,existing,captured);
        if(existing)CloseHandle(existing);return result;
    }
    wchar_t full[MAX_PATH]; GetFullPathNameW(dll.c_str(), MAX_PATH, full, nullptr); dll = full;
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) { EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"dll not found: %s\"}\n", JEsc(W2U(dll)).c_str()); return 2; }
    // load a per-build COPY (dist\run\<name>_<mtime>.dll) so the dist DLL is never locked by a running game
    // and build.bat can always replace it; games injected earlier keep their own copy.
    {
        WIN32_FILE_ATTRIBUTE_DATA fa; GetFileAttributesExW(dll.c_str(), GetFileExInfoStandard, &fa);
        std::wstring dir = dll.substr(0, dll.find_last_of(L'\\')) + L"\\run";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring base = dll.substr(dll.find_last_of(L'\\') + 1); base = base.substr(0, base.size() - 4);
        wchar_t name[64]; swprintf_s(name, L"_%08lX%08lX.dll", fa.ftLastWriteTime.dwHighDateTime, fa.ftLastWriteTime.dwLowDateTime);
        std::wstring copy = dir + L"\\" + base + name;
        if (GetFileAttributesW(copy.c_str()) == INVALID_FILE_ATTRIBUTES && !CopyFileW(dll.c_str(), copy.c_str(), TRUE)) {
            EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"cannot copy dll to %s\"}\n", JEsc(W2U(copy)).c_str()); return 2;
        }
        dll = copy;
        TargetToken tt = QueryTargetToken(pid);
        if (tt.appContainer) {
            // an AppContainer can neither read our DLL nor write our log dir unless the AC groups are granted
            std::string e;
            if (!GrantAppContainers(copy, GENERIC_READ | GENERIC_EXECUTE, false, e)) { EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"AppContainer target: cannot grant read/execute on %s: %s\"}\n", JEsc(W2U(copy)).c_str(), e.c_str()); return 2; }
            std::wstring logs = dir + L"\\logs"; CreateDirectoryW(logs.c_str(), nullptr);
            if (!GrantAppContainers(logs, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE, true, e)) fprintf(stderr, "warning: AppContainer log dir grant failed: %s\n", e.c_str());
            if (tt.acPath.empty()) { EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"AppContainer target: GetAppContainerNamedObjectPath failed, pipe path unknown\"}\n"); return 2; }
        }
    }
    HANDLE hp = OpenProcess(SYNCHRONIZE | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hp) { EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"OpenProcess failed %lu\"}\n", GetLastError()); return 2; }
    SIZE_T sz = (dll.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(hp, nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SIZE_T written=0;
    if(!mem||!WriteProcessMemory(hp,mem,dll.c_str(),sz,&written)||written!=sz){const auto error=GetLastError();if(mem)VirtualFreeEx(hp,mem,0,MEM_RELEASE);CloseHandle(hp);EmitInjectReply(output,captured,"{\"ok\":false,\"pid\":%lu,\"win32_error\":%lu,\"error\":\"remote DLL-path allocation/write failed before LoadLibrary was requested\"}",pid,error);return 2;}
    auto ll = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if(!CliBudget(1)){VirtualFreeEx(hp,mem,0,MEM_RELEASE);CloseHandle(hp);EmitInjectReply(output,captured,"{\"ok\":false,\"pid\":%lu,\"injected\":null,\"error\":\"launch deadline expired before LoadLibrary was requested\"}",pid);return 2;}
    HANDLE th = CreateRemoteThread(hp, nullptr, 0, ll, mem, 0, nullptr);
    if (!th) {const auto error=GetLastError();VirtualFreeEx(hp,mem,0,MEM_RELEASE);EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"CreateRemoteThread failed %lu\"}\n",error); CloseHandle(hp); return 2; }
    const DWORD waited=WaitForSingleObject(th,CliBudget(20000));const bool completed=waited==WAIT_OBJECT_0;
    DWORD code=0;const bool codeKnown=completed&&GetExitCodeThread(th,&code)!=FALSE;CloseHandle(th);
    if(vi_cli_deadline::RemoteStorageMayBeFreed(true,completed))VirtualFreeEx(hp,mem,0,MEM_RELEASE);
    if(!completed||!codeKnown){if(!PrintInjectionExit(output,hp,pid,"load-library-wait",0,captured))EmitInjectReply(output,captured,"{\"ok\":false,\"pid\":%lu,\"injected\":null,\"load_library_completion_confirmed\":%s,\"load_library_may_still_be_running\":%s,\"remote_path_storage_retained\":%s,\"wait_result\":%lu,\"error\":\"LoadLibrary result was not confirmed within the injection/launch deadline; inspect pending/storage flags; injection was not retried\"}",pid,completed?"true":"false",completed?"false":"true",completed?"false":"true",waited);CloseHandle(hp);return 2;}
    if (PrintInjectionExit(output, hp, pid, "load-library",0,captured)) { CloseHandle(hp); return 2; }
    if (!code) { CloseHandle(hp); EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"LoadLibraryW returned NULL in target\"}\n"); return 2; }
    if (!ReadinessTransport(pid, "{\"cmd\":\"ping\"}",20000,resp,hp)) {
        // A dying process can close its pipe just before its process object signals.
        // Observe that exact object briefly; a live server failure remains a generic pipe error.
        if (!PrintInjectionExit(output, hp, pid, "pipe-wait", 100,captured)) EmitInjectReply(output,captured, "{\"ok\":false,\"error\":\"dll loaded but pipe never came up\"}\n");
        CloseHandle(hp); return 2;
    }
    int result=ReportInputReady(output,pid,dll,quiet,hooksOnly,false,hp,captured);
    CloseHandle(hp);return result;
}

// ------------------------------------------------------------------------------------------
// launch: lock -> start -> wait window -> inject -> settle -> unlock
// ------------------------------------------------------------------------------------------
static vi_session::JsonLock g_launchLock;
static std::atomic<HWND> g_previousForeground{nullptr};
static std::atomic<DWORD> g_launchPid{0}, g_initialLaunchPid{0};
// Restore destinations, newest first: the window that was foreground before launch, then any non-target window
// that became foreground during the launch (a transient one, e.g. a Steam dialog that has closed, is skipped).
static std::vector<HWND> g_fgHistory;
static std::mutex g_fgStateMx;
static std::recursive_mutex g_restoreMx;
static std::atomic<const char*> g_launchPhase{"startup"};
#include "vi_launch_live.h"
static vi_user_intent::Detector g_launchUserIntent;
static std::atomic<bool> g_launchSawTargetForeground{false};
static bool HumanSelectedTarget(HWND foreground) {
    DWORD holder=0;if(foreground)GetWindowThreadProcessId(foreground,&holder);
    DWORD pid=g_launchPid.load();
    if(holder==g_initialLaunchPid.load())pid=holder;
    HWND target=pid?VisibleWindowOf(pid):nullptr;
    if(!target && holder==pid)target=foreground;
    g_launchUserIntent.SetTarget(target,pid);
    return g_launchUserIntent.Refresh(foreground);
}
static void NoteForeground(HWND fg) {
    if (!fg) return;
    std::lock_guard<std::mutex> lk(g_fgStateMx);
    if (!g_fgHistory.empty() && g_fgHistory.back() == fg) return;
    g_fgHistory.erase(std::remove(g_fgHistory.begin(), g_fgHistory.end(), fg), g_fgHistory.end());
    g_fgHistory.push_back(fg);
}
static bool TargetHoldsForeground(HWND fg, DWORD* holder = nullptr) {
    if (!fg) return false;
    DWORD p = 0; GetWindowThreadProcessId(fg, &p);
    if (holder) *holder = p;
    if (!p) return false;
    return p == g_launchPid || p == g_initialLaunchPid || ForegroundIsTarget(fg,g_launchPid) || ForegroundIsTarget(fg,g_initialLaunchPid);
}
static bool LaunchUserTookForeground(){return vi_launch_live::UserTookForeground(g_launchUserIntent.Active(),g_launchSawTargetForeground,TargetHoldsForeground(GetForegroundWindow()));}
static bool IsRestoreDestination(HWND window) {
    if(!window || !IsWindow(window) || !IsWindowVisible(window))return false;
    DWORD owner=0;GetWindowThreadProcessId(window,&owner);
    return owner && owner!=g_launchPid && owner!=g_initialLaunchPid &&
        !ForegroundIsTarget(window,g_launchPid) && !ForegroundIsTarget(window,g_initialLaunchPid);
}
static HWND PickRestoreTarget() {
    std::lock_guard<std::mutex> lk(g_fgStateMx);
    for (size_t i = g_fgHistory.size(); i-- > 0;) {
        HWND h = g_fgHistory[i];
        if (IsRestoreDestination(h)) return h;
    }
    HWND previous = g_previousForeground.load();
    if(IsRestoreDestination(previous))return previous;
    HWND shell=GetShellWindow();return IsRestoreDestination(shell) ? shell : nullptr;
}
// A UI-blocked restore worker must not stall the launch watcher. This deadline
// applies only to its local pipe fallback; ordinary command/hold timeouts stay unchanged.
static ULONGLONG ForegroundTick() {return (ULONGLONG)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static bool RestorePipeRequest(DWORD pid,const std::string& command,bool& timedOut) {
    timedOut=false;const ULONGLONG deadline=ForegroundTick()+40;
    struct Pipe {HANDLE h=INVALID_HANDLE_VALUE;~Pipe(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}} pipe;
    const auto name=PipeName(pid);
    while(ForegroundTick()<deadline){
        pipe.h=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(pipe.h!=INVALID_HANDLE_VALUE)break;Sleep(1);
    }
    if(pipe.h==INVALID_HANDLE_VALUE){timedOut=ForegroundTick()>=deadline;return false;}
    auto transfer=[&](bool write,void* data,DWORD bytes,DWORD& done)->bool {
        OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!operation.hEvent)return false;
        BOOL ok=write?WriteFile(pipe.h,data,bytes,&done,&operation):ReadFile(pipe.h,data,bytes,&done,&operation);
        if(!ok && GetLastError()==ERROR_IO_PENDING){
            ULONGLONG now=ForegroundTick();DWORD wait=now<deadline?(DWORD)(deadline-now):0;
            DWORD result=WaitForSingleObject(operation.hEvent,wait);
            if(result!=WAIT_OBJECT_0){
                timedOut=result==WAIT_TIMEOUT;CancelIoEx(pipe.h,&operation);
                // Local named-pipe cancellation completes before releasing OVERLAPPED storage.
                GetOverlappedResult(pipe.h,&operation,&done,TRUE);CloseHandle(operation.hEvent);return false;
            }
            ok=GetOverlappedResult(pipe.h,&operation,&done,FALSE);
        } else if(ok)ok=GetOverlappedResult(pipe.h,&operation,&done,FALSE);
        CloseHandle(operation.hEvent);return ok!=FALSE;
    };
    std::string line=StampOwner(command)+"\n";DWORD done=0;
    if(!transfer(true,&line[0],(DWORD)line.size(),done)||done!=line.size())return false;
    std::string reply;
    while(ForegroundTick()<deadline){
        DWORD available=0;if(!PeekNamedPipe(pipe.h,nullptr,0,nullptr,&available,nullptr))return false;
        if(!available){Sleep(1);continue;}
        char buffer[4096];done=0;
        if(!transfer(false,buffer,(std::min)(available,(DWORD)sizeof(buffer)),done)||!done)return false;
        reply.append(buffer,done);if(reply.find('\n')!=std::string::npos)return true;
        if(reply.size()>65536)return false;
    }
    timedOut=true;return false;
}
static bool RestoreForeground() {
    std::lock_guard<std::recursive_mutex> lk(g_restoreMx);
    const ULONGLONG began=ForegroundTick();
    MSG queue; PeekMessageW(&queue,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    HWND current = GetForegroundWindow();
    if(HumanSelectedTarget(current) || g_launchUserIntent.EvaluatingAltTab())return true;
    DWORD foregroundPid = 0; GetWindowThreadProcessId(current, &foregroundPid);
    if (!foregroundPid) return true;
    if (!TargetHoldsForeground(current)) return true; // respect a human's newer choice
    DWORD pipePid=foregroundPid;
    if(foregroundPid!=g_launchPid && ForegroundIsTarget(current,g_launchPid))pipePid=g_launchPid;
    else if(foregroundPid!=g_initialLaunchPid && ForegroundIsTarget(current,g_initialLaunchPid))pipePid=g_initialLaunchPid;
    HWND previous = PickRestoreTarget();
    if (!previous || previous == current) return false;
    // Try the OS hand-back first: the DLL/pipe may not exist yet. In particular, do not spend
    // the entire injection/adoption interval with a newly-created game in the foreground.
    if(GetForegroundWindow()!=current)return !TargetHoldsForeground(GetForegroundWindow());
    ULONGLONG stage=ForegroundTick();SetLastError(0);BOOL first=SetForegroundWindow(previous);DWORD firstError=GetLastError();
    ULONGLONG firstMs=ForegroundTick()-stage,attachMs=0,pipeMs=0;BOOL attached=FALSE,second=FALSE,top=FALSE;DWORD attachError=0;bool pipeReceived=false,pipeTimedOut=false;
    if (GetForegroundWindow() == current) {
        // a UWP frame is owned by ApplicationFrameHost, which runs no vinput: share its input state for the call
        // (AttachThreadInput is permitted toward a lower/equal integrity thread; no OS input is generated)
        DWORD fgThread = GetWindowThreadProcessId(current, nullptr), me = GetCurrentThreadId();
        stage=ForegroundTick();
        SetLastError(0);if(fgThread && fgThread != me)attached=AttachThreadInput(me,fgThread,TRUE);attachError=GetLastError();
        if (attached) {
            if(GetForegroundWindow()==current)second=SetForegroundWindow(previous);
            if(GetForegroundWindow()==current)top=BringWindowToTop(previous);
            AttachThreadInput(me, fgThread, FALSE);
        }
        attachMs=ForegroundTick()-stage;
    }
    if (GetForegroundWindow() == current) {
        char hwnd[32]; sprintf_s(hwnd, "0x%llX", (unsigned long long)(ULONG_PTR)previous);
        stage=ForegroundTick();pipeReceived=RestorePipeRequest(pipePid,"{\"cmd\":\"restore-foreground\",\"hwnd\":\""+std::string(hwnd)+"\"}",pipeTimedOut);pipeMs=ForegroundTick()-stage;
    }
    stage=ForegroundTick();
    if(GetForegroundWindow()==current) {
        // One high-resolution observation wait avoids Sleep(1)'s ~16ms rounding.
        // Scheduler/native-call overruns remain visible in elapsed telemetry.
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        if(timer){
            ULONGLONG used=ForegroundTick()-stage;DWORD remaining=used<25?(DWORD)(25-used):0;
            LARGE_INTEGER due{};due.QuadPart=-(LONGLONG)remaining*10000;
            if(remaining && SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer,remaining);
            CloseHandle(timer);
        } else while(GetForegroundWindow()==current && ForegroundTick()-stage<25)Sleep(1);
    }
    bool restored = !TargetHoldsForeground(GetForegroundWindow());
    fprintf(stderr,"[vinput] foreground restore %s previous=%p target_pid=%lu elapsed_ms=%llu first_ms=%llu first_ok=%d first_error=%lu attach_ms=%llu attached=%d attach_error=%lu second=%d top=%d pipe_ms=%llu pipe_received=%d pipe_timeout=%d poll_ms=%llu actual=%p\n",
        restored?"ok":"FAILED",previous,foregroundPid,ForegroundTick()-began,firstMs,(int)first,firstError,attachMs,(int)attached,attachError,(int)second,(int)top,pipeMs,(int)pipeReceived,(int)pipeTimedOut,ForegroundTick()-stage,GetForegroundWindow());
    return restored;
}
// Delayed foreground guard. A launch can lose the real foreground AFTER its first successful restore: e.g. Steam's
// launch dialog closes ~1.5 s later and Windows activates the next window in z-order, the game
// (measured on a real Steam game). For --fg-guard seconds after the restore, launch keeps polling; whenever the owned target (or the
// process launch itself started) holds the real foreground, it is handed back and the re-grab is reported.
// A window outside the target that becomes foreground meanwhile becomes the new restore destination (a human's newer
// choice is respected); validated physical target selection suspends the guard until that target loses foreground.
struct FgRegrab { DWORD t_ms; DWORD pid; bool restored; const char* phase; };
static std::vector<FgRegrab> g_regrabs;
static DWORD g_launchT0 = 0;
static DWORD g_foregroundRetryAt = 0; // protected by g_restoreMx, like each restore attempt
static void GuardForeground(DWORD ms, const char* phase) {
    if (ms) g_launchPhase = phase;
    DWORD start = GetTickCount();
    for (;;) {
      {
        std::lock_guard<std::recursive_mutex> guard(g_restoreMx);
        HWND fg = GetForegroundWindow(); DWORD holder = 0;
        const bool targetForeground=TargetHoldsForeground(fg,&holder);
        if(targetForeground)g_launchSawTargetForeground=true;
        bool human=HumanSelectedTarget(fg);
        if (TargetHoldsForeground(fg, &holder) && !human && !g_launchUserIntent.EvaluatingAltTab() && (!g_foregroundRetryAt || (LONG)(GetTickCount()-g_foregroundRetryAt)>=0)) {
            bool restored = RestoreForeground();
            g_foregroundRetryAt=restored?0:GetTickCount()+25;
            { std::lock_guard<std::mutex> lk(g_fgStateMx); g_regrabs.push_back({GetTickCount() - g_launchT0, holder, restored, phase}); }
            fprintf(stderr, "[vinput] %s: target pid %lu took the real foreground at +%lu ms -> %s\n", phase, holder, GetTickCount() - g_launchT0, restored ? "handed back" : "restore FAILED");
        } else if (fg && !TargetHoldsForeground(fg)) NoteForeground(fg);
      }
        if (GetTickCount() - start >= ms) break;
        Sleep(15);
    }
}
static std::string RegrabsJson() {
    std::lock_guard<std::mutex> lk(g_fgStateMx);
    std::string s = "[";
    for (size_t i = 0; i < g_regrabs.size(); i++) {
        char b[160]; sprintf_s(b, "%s{\"t_ms\":%lu,\"pid\":%lu,\"restored\":%s,\"phase\":\"%s\"}", i ? "," : "", g_regrabs[i].t_ms, g_regrabs[i].pid, g_regrabs[i].restored ? "true" : "false", g_regrabs[i].phase);
        s += b;
    }
    return s + "]";
}
static void ReleaseLock() {
    if (g_launchLock.Held()) { std::string error;
        if (g_launchLock.Release(error)) fprintf(stderr, "[vinput] lock released\n");
        else fprintf(stderr, "[vinput] lock release FAILED: %s\n", error.c_str());
    }
}
static BOOL WINAPI CtrlHandler(DWORD) { RestoreForeground(); ReleaseLock(); return FALSE; }
struct LockWaitResult {
    bool acquired = false, timedOut = false;
    ULONGLONG elapsedMs = 0;
    unsigned attempts = 0;
    std::string lastError;
};
static LockWaitResult AcquireLock(const std::wstring& path, unsigned retrySec, unsigned maxWaitSec) {
    const ULONGLONG started = GetTickCount64(), budgetMs = (ULONGLONG)maxWaitSec * 1000;
    LockWaitResult result;
    for (;;) {
        result.elapsedMs = GetTickCount64() - started;
        if (result.attempts && budgetMs && result.elapsedMs >= budgetMs) { result.timedOut = true; return result; }
        ++result.attempts;
        if (g_launchLock.Acquire(path, "vinput-launch-" + std::to_string(GetCurrentProcessId()), 600, result.lastError)) {
            SetConsoleCtrlHandler(CtrlHandler, TRUE);
            fprintf(stderr, "[vinput] lock acquired\n");
            result.acquired = true; result.elapsedMs = GetTickCount64() - started; return result;
        }
        result.elapsedMs = GetTickCount64() - started;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) { fprintf(stderr, "[vinput] lock create failed: %s\n", result.lastError.c_str()); return result; }
        if (budgetMs && result.elapsedMs >= budgetMs) { result.timedOut = true; return result; }
        DWORD sleepMs = retrySec * 1000;
        if (budgetMs) sleepMs = (DWORD)(std::min)((ULONGLONG)sleepMs, budgetMs - result.elapsedMs);
        fprintf(stderr, "[vinput] lock busy (%s); retry interval=%u s, next attempt in %lu ms, total wait=%s\n",
            result.lastError.c_str(), retrySec, sleepMs, maxWaitSec ? (std::to_string(maxWaitSec) + " s").c_str() : "unlimited (set --lock-timeout SECONDS to bound it)");
        Sleep(sleepMs);
    }
}
static std::string GetOpt(const Args& a, const char* k, const char* d = "") { for (auto& o : a.opt) if (o.first == k) return o.second; return d; }
static bool HasOpt(const Args& a, const char* k) { for (auto& o : a.opt) if (o.first == k) return true; return false; }
static bool LockSeconds(const std::string& text, unsigned minimum, unsigned& seconds) {
    seconds = 0;
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9' || seconds > (86400u - (unsigned)(c - '0')) / 10u) return false;
        seconds = seconds * 10u + (unsigned)(c - '0');
    }
    return seconds >= minimum;
}

static std::string JsonText(const std::string& json, const char* key) {
    std::string mark = "\"" + std::string(key) + "\":\"";
    size_t p = json.find(mark); if (p == std::string::npos) return {};
    p += mark.size(); size_t end = json.find('"', p); return end == std::string::npos ? std::string() : json.substr(p, end-p);
}
struct Reply { bool ok = false; std::string json; };
static Reply Fail(const std::string& error, const std::string& extra = std::string()) {
    return {false, "{\"ok\":false,\"error\":\"" + JEsc(error) + "\"" + (extra.empty() ? "" : "," + extra) + "}"};
}
static Reply WaitChange(DWORD pid, const Args& a) {
    double duration = atof(GetOpt(a, "timeout", "10").c_str()) * 1000;
    if (HasOpt(a, "timeout-ms")) duration = atof(GetOpt(a, "timeout-ms").c_str());
    if (!(duration >= 1 && duration <= 120000)) return Fail("timeout must be 1..120000 ms");
    DWORD timeout = (DWORD)duration, start = GetTickCount();
    std::string command = "{"; PutS(command, "cmd", "frame-hash");
    if (HasOpt(a, "region")) PutS(command, "region", GetOpt(a, "region"));
    if (HasOpt(a, "method")) PutS(command, "method", GetOpt(a, "method"));
    command += "}";
    std::string baseline, reply;
    if (!Request(pid, command, reply) || (baseline = JsonText(reply, "hash")).empty()) return {false, reply};
    unsigned samples = 1;
    while (GetTickCount()-start < timeout) {
        Sleep(50);
        if (!Request(pid, command, reply) || JsonText(reply, "hash").empty()) return {false, reply};
        ++samples;
        std::string current = JsonText(reply, "hash");
        if (current != baseline) {
            char text[256]; sprintf_s(text, "{\"ok\":true,\"changed\":true,\"elapsed_ms\":%lu,\"samples\":%u,\"before_hash\":\"%s\",\"hash\":\"%s\"}", GetTickCount()-start, samples, baseline.c_str(), current.c_str());
            return {true, text};
        }
    }
    char text[256]; sprintf_s(text, "{\"ok\":false,\"changed\":false,\"error\":\"screen did not change before timeout\",\"elapsed_ms\":%lu,\"samples\":%u}", GetTickCount()-start, samples);
    return {false, text};
}

#include "vi_cli_ui.inl"
#include "vi_cli_doctor.inl"
#include "vi_cli_launch_options.inl"
#include <limits>
#include "vi_cli_launch_selection.inl"
#include "vi_launch_injection_failure.h"
#include "vi_cli_launch_follow.inl"
#include "vi_cli_hidden_window.inl"

static int Launch(const Args& a) {
    std::string injectReply,earlyInjectReply,dialogInjectReply,launchReadiness;
    std::string finalCommandLine,appliedAffinity,hiddenProbeInject,hiddenReason,showHiddenReply,showHiddenStatus;DWORD spawnedPid=0;bool affinityApplied=false,showHidden=false;
    bool windowHiddenKnown=false,windowHidden=false,windowAdopted=false,showHiddenConfirmed=false,startupSplashSeen=false;
    DWORD guardAppliedMs=0,settleAppliedMs=0,guardRequestedMs=0,settleRequestedMs=0;const DWORD finalReserveMs=1500;
    ULONGLONG budgetStarted=0;DWORD budgetRequested=0;
    DWORD followedFrom=0,launcherPid=0;
    unsigned long long selectedCreated=0;DWORD pid=0,preResumeGuardPid=0;
    std::string waitingFor;
    // Every launch result adds the actual latest injection record. Early hooks-only
    // setup remains separate from the full window/queue handshake after resume.
    auto launchResult=[&](const char* format,auto... values) {
        std::string result;EmitInjectReply(nullptr,&result,format,values...);
        if(!result.empty()&&result.back()=='}')result.pop_back();
        result+=",\"inject\":"+(injectReply.empty()?std::string("null"):injectReply);
        result+=",\"launch_readiness\":"+(launchReadiness.empty()?std::string("null"):launchReadiness);
        result+=",\"early_inject\":"+(earlyInjectReply.empty()?std::string("null"):earlyInjectReply);
        result+=",\"dialog_inject\":"+(dialogInjectReply.empty()?std::string("null"):dialogInjectReply)+"}";
        result.pop_back();
        result+=",\"startup_splash_observed\":"+std::string(startupSplashSeen?"true":"false");
        result+=",\"followed_from\":"+(followedFrom?std::to_string(followedFrom):"null");
        result+=",\"launcher_pid\":"+(launcherPid?std::to_string(launcherPid):"null");
        result+=",\"process_created\":\""+std::to_string(selectedCreated)+"\"";
        result+=",\"launch_state\":\""+std::string(launcherPid?"launcher_ready":"game")+"\"";
        result+=",\"waiting_for\":"+(waitingFor.empty()?std::string("null"):"\""+JEsc(waitingFor)+"\"")+"}";
        result.pop_back();result+=vi_launch_exposure::Fields(pid,preResumeGuardPid,budgetStarted!=0,g_launchSawTargetForeground)+"}";
        result.pop_back();result+=",\"final_command_line\":"+(finalCommandLine.empty()?std::string("null"):"\""+JEsc(finalCommandLine)+"\"");
        result+=",\"spawned_pid\":"+(spawnedPid?std::to_string(spawnedPid):"null")+",\"requested_cpu_affinity\":"+(HasOpt(a,"cpu-affinity")?"\""+JEsc(GetOpt(a,"cpu-affinity"))+"\"":"null");
        result+=",\"applied_cpu_affinity\":"+(appliedAffinity.empty()?std::string("null"):"\""+appliedAffinity+"\"")+",\"cpu_affinity_applied\":"+(affinityApplied?"true":"false")+"}";
        result.pop_back();result+=",\"window_hidden\":"+(windowHiddenKnown?std::string(windowHidden?"true":"false"):"null")+",\"window_adopted\":"+(windowAdopted?"true":"false")+",\"hidden_discovery_inject\":"+(hiddenProbeInject.empty()?std::string("null"):hiddenProbeInject)+",\"hidden_window_reason\":"+(hiddenReason.empty()?std::string("null"):"\""+JEsc(hiddenReason)+"\"")+"}";
        result.pop_back();result+=",\"show_hidden_requested\":"+std::string(showHidden?"true":"false")+",\"show_hidden_reply\":"+(showHiddenReply.empty()?std::string("null"):showHiddenReply)+",\"show_hidden_status\":"+(showHiddenStatus.empty()?std::string("null"):showHiddenStatus)+"}";
        if(showHidden){result.pop_back();result+=",\"show_hidden_visibility_confirmed\":"+std::string(showHiddenConfirmed?"true":"false")+(showHiddenConfirmed?"":",\"show_hidden_warning\":\"visibility was not confirmed within the bounded observation; inspect the native pending/expiry receipt; no show request was replayed\"")+"}";}
        if(budgetStarted){result.pop_back();result+=",\"launch_budget_ms\":"+std::to_string(budgetRequested)+",\"launch_elapsed_ms\":"+std::to_string(GetTickCount64()-budgetStarted)+",\"launch_deadline_exceeded\":"+(CliBudget(1)?"false":"true")+",\"fg_guard_requested_ms\":"+std::to_string(guardRequestedMs)+",\"fg_guard_applied_ms\":"+std::to_string(guardAppliedMs)+",\"settle_requested_ms\":"+std::to_string(settleRequestedMs)+",\"settle_applied_ms\":"+std::to_string(settleAppliedMs)+",\"finalization_reserve_ms\":"+std::to_string(finalReserveMs)+",\"launch_observation_truncated\":"+((guardAppliedMs<guardRequestedMs||settleAppliedMs<settleRequestedMs)?"true":"false")+"}";}
        printf("%s\n",result.c_str());
    };
    std::wstring exe = U2W(GetOpt(a, "exe")), steam = U2W(GetOpt(a, "steam")), args = U2W(GetOpt(a, "args")), waitExe = U2W(GetOpt(a, "wait-exe")), aumid = U2W(GetOpt(a, "aumid"));
    std::wstring lock = U2W(GetOpt(a, "lock")), dll = U2W(GetOpt(a, "dll"));
    DWORD followPid=0;
    if(HasOpt(a,"follow-pid")){
        const auto value=GetOpt(a,"follow-pid");char* end=nullptr;const auto n=strtoull(value.c_str(),&end,10);
        if(!n||!end||*end||n>MAXDWORD){launchResult("{\"ok\":false,\"error\":\"--follow-pid needs a positive PID\"}");return 2;}followPid=(DWORD)n;
    }
    if(HasOpt(a,"follow-created")&&!followPid){launchResult("{\"ok\":false,\"error\":\"--follow-created requires --follow-pid\"}");return 2;}
    LaunchOptions launchOptions; std::string launchOptionError;
    if (!PrepareLaunchOptions(a, exe, launchOptions, launchOptionError)) { launchResult("{\"ok\":false,\"error\":\"%s\"}\n", JEsc(launchOptionError).c_str()); return 2; }
    bool allowDialog=false;unsigned allowCount=0;
    for(const auto& option:a.opt)if(option.first=="allow-dialog"){if(++allowCount>1||(option.second!="true"&&option.second!="false")){launchResult("{\"ok\":false,\"error\":\"--allow-dialog must appear once with true or false\"}\n");return 2;}allowDialog=option.second=="true";}
    unsigned showCount=0;for(const auto& option:a.opt)if(option.first=="show-hidden"){if(++showCount>1||(option.second!="true"&&option.second!="false")){launchResult("{\"ok\":false,\"error\":\"--show-hidden must appear once with true or false; no target was started\"}");return 2;}showHidden=option.second=="true";}
    unsigned timeout=0,settle=0;
    if(!LockSeconds(GetOpt(a,"timeout","240"),1,timeout)||timeout>1800||!LockSeconds(GetOpt(a,"settle","5"),0,settle)||settle>600){launchResult("{\"ok\":false,\"error\":\"timeout must be 1..1800 and settle 0..600 whole seconds\"}");return 2;}
    unsigned retry = 0, lockTimeout = 0;
    if (HasOpt(a, "lock-timeout") && HasOpt(a, "lock-max-wait")) { launchResult("{\"ok\":false,\"error\":\"use only --lock-timeout or its existing --lock-max-wait alias\"}\n"); return 2; }
    if (!LockSeconds(GetOpt(a, "lock-retry", "60"), 1, retry)) { launchResult("{\"ok\":false,\"error\":\"--lock-retry is the interval between attempts: use 1..86400 whole seconds; set --lock-timeout for a total wait budget\"}\n"); return 2; }
    if (!LockSeconds(GetOpt(a, "lock-timeout", GetOpt(a, "lock-max-wait", "0").c_str()), 0, lockTimeout)) { launchResult("{\"ok\":false,\"error\":\"--lock-timeout (or --lock-max-wait) must be 0..86400 whole seconds; 0 keeps the legacy unlimited wait\"}\n"); return 2; }
    if ((int)!exe.empty()+(int)!steam.empty()+(int)!aumid.empty()+(int)(followPid!=0)!=1) { launchResult("{\"ok\":false,\"error\":\"launch needs exactly one of --exe, --steam, --aumid or --follow-pid\"}\n"); return 2; }
    if (!steam.empty() && waitExe.empty()) { launchResult("{\"ok\":false,\"error\":\"--steam needs --wait-exe <name.exe>\"}\n"); return 2; }
    bool early = HasOpt(a, "early");
    if (early && exe.empty()) { launchResult("{\"ok\":false,\"error\":\"--early needs --exe (the process must be created suspended by vinput)\"}\n"); return 2; }
    if (early && HasOpt(a, "no-inject")) { launchResult("{\"ok\":false,\"error\":\"--early and --no-inject contradict each other\"}\n"); return 2; }
    double guardSec = atof(GetOpt(a, "fg-guard", "5").c_str());
    if (!(guardSec >= 0 && guardSec <= 120)) { launchResult("{\"ok\":false,\"error\":\"--fg-guard must be 0..120 seconds\"}\n"); return 2; }
    guardRequestedMs=(DWORD)(guardSec*1000);settleRequestedMs=(DWORD)settle*1000;
    std::string claimTtl = GetOpt(a, "claim-ttl", "300");
    if (!HasOpt(a, "no-inject")) {
        vi_ac::Finding f = exe.empty() ? vi_ac::Finding() : vi_ac::CheckExe(exe);
        if (exe.empty()) vi_ac::ScanSystem(f);
        if (f.refused) { printf("%s\n", AcRefusalJson(f, 0).c_str()); return 4; }
    }
    g_acSuspended = early;
    if (!lock.empty()) {
        LockWaitResult wait = AcquireLock(lock, retry, lockTimeout);
        if (!wait.acquired) {
            launchResult("{\"ok\":false,\"error\":\"%s\",\"lock_timed_out\":%s,\"lock_wait_ms\":%llu,\"lock_attempts\":%u,\"lock_timeout_seconds\":%u,\"lock_retry_seconds\":%u,\"lock_holder_hint\":\"%s\"}\n",
                wait.timedOut ? "lock wait timed out; no target launched" : "could not acquire lock; no target launched", wait.timedOut ? "true" : "false", wait.elapsedMs, wait.attempts, lockTimeout, retry, JEsc(wait.lastError).c_str());
            return 3;
        }
    }
    budgetStarted=GetTickCount64();budgetRequested=(DWORD)timeout*1000;CliDeadlineScope launchDeadline(budgetRequested);
    auto optionalBudget=[&](DWORD requested){return vi_cli_deadline::Optional(g_cliDeadline,GetTickCount64(),requested,finalReserveMs);};
    struct ReadyWaitScope{DWORD old;ReadyWaitScope():old(g_cliReadinessWaitMs){g_cliReadinessWaitMs=1000;}~ReadyWaitScope(){g_cliReadinessWaitMs=old;}} readyWaitScope;
    g_previousForeground = GetForegroundWindow(); NoteForeground(g_previousForeground);
    g_launchT0 = GetTickCount();g_launchSawTargetForeground=false;
    // Start before CreateProcess, not after injection. This remains live while the main thread
    // waits for a process/window, injects, waits for adoption, claims, settles, and reads the pipe.
    std::atomic<bool> watching{true};
    g_launchUserIntent.Configure({GetAsyncKeyState,GetCursorPos,WindowFromPoint,GetForegroundWindow});
    std::thread watcher([&] { g_launchUserIntent.InstallEvents();while (watching) { {std::lock_guard<std::recursive_mutex> guard(g_restoreMx);g_launchUserIntent.Pump();GuardForeground(0, g_launchPhase.load());} Sleep(10); }g_launchUserIntent.StopEvents(); });
    int rc = 2; bool earlyInjected = false;
    LaunchProcessHandles processHandles;
    vi_launch_follow::Process rootProcess{};
    const auto initialProcesses=LaunchSnapshot(nullptr,waitExe);
    FILETIME launchTime{};GetSystemTimeAsFileTime(&launchTime);
    std::string cwdFields;
    auto rejectDialog = [&](HWND window,bool injected) {
        if((WindowClass(window)!="#32770"&&!OwnedPopupWindow(window)) || allowDialog || launcherPid==pid)return false;
        DialogText text;text.pid=pid;EnumChildWindows(window,DialogTextEnum,(LPARAM)&text);
        RestoreForeground();
        launchResult("{\"ok\":false,\"pid\":%lu,\"injected\":%s,\"error\":\"launch found a modal/error dialog instead of a game window; inspect dialog_text (use --allow-dialog only for an intentional dialog target)\",\"dialog_text\":\"%s\",\"foreground_restored\":%s%s}\n",
            pid,injected?"true":"false",JEsc(W2U(text.text)).c_str(),TargetHoldsForeground(GetForegroundWindow())?"false":"true",LaunchDialogFields(window).c_str());
        return true;
    };
    do {
        if(!CliBudget(1)){launchResult("{\"ok\":false,\"error\":\"launch deadline expired before target selection; no additional process or injection was requested\"}");break;}
        if (!exe.empty()) {
            std::wstring dir = exe; size_t sl = dir.find_last_of(L"\\/"); dir = sl == std::wstring::npos ? L"." : dir.substr(0, sl);
            if (launchOptions.explicitCwd) dir = launchOptions.cwd;
            std::wstring reportedCwd; if (LaunchAbsolutePath(dir, reportedCwd)) cwdFields = ",\"cwd\":\"" + JEsc(W2U(reportedCwd)) + "\",\"cwd_source\":\"" + (launchOptions.explicitCwd ? "explicit" : "exe_directory") + "\"";
            std::wstring cl = L"\"" + exe + L"\" " + args;finalCommandLine=W2U(cl);
            STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi;
            si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWNOACTIVATE;
            const bool suspended=early||launchOptions.affinitySpecified;
            if (!CreateProcessW(exe.c_str(), &cl[0], nullptr, nullptr, FALSE, suspended ? CREATE_SUSPENDED : 0, nullptr, dir.c_str(), &si, &pi)) {const auto error=GetLastError();launchResult("{\"ok\":false,\"win32_error\":%lu,\"error\":\"CreateProcess failed %lu%s\"}\n",error,error,error==ERROR_ELEVATION_REQUIRED?": executable requires elevation; no game was started; use an elevated vinput session":""); break; }
            pid = pi.dwProcessId;spawnedPid=pid; g_initialLaunchPid = pid; g_launchPid = pid;
            rootProcess=LaunchProcess(pid,GetCurrentProcessId(),ProcName(pid),pi.hProcess);processHandles.Retain(rootProcess,pi.hProcess);
            if (!cwdFields.empty()) cwdFields += ",\"cwd_applies_to_spawned_pid\":" + std::to_string(pid);
            if(launchOptions.affinitySpecified){
                if(!SetProcessAffinityMask(pi.hProcess,(DWORD_PTR)launchOptions.affinityMask)){
                    const auto error=GetLastError();const bool terminated=TerminateProcess(pi.hProcess,1)!=FALSE;
                    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
                    launchResult("{\"ok\":false,\"pid\":%lu,\"win32_error\":%lu,\"suspended_child_terminated\":%s,\"error\":\"could not apply requested CPU affinity before startup; child was never resumed\"}",pid,error,terminated?"true":"false");break;
                }
                DWORD_PTR actual=0,system=0;if(GetProcessAffinityMask(pi.hProcess,&actual,&system)){char applied[32];sprintf_s(applied,"0x%llX",(unsigned long long)actual);appliedAffinity=applied;affinityApplied=actual==(DWORD_PTR)launchOptions.affinityMask;}
                if(!affinityApplied){const bool terminated=TerminateProcess(pi.hProcess,1)!=FALSE;CloseHandle(pi.hThread);CloseHandle(pi.hProcess);launchResult("{\"ok\":false,\"pid\":%lu,\"suspended_child_terminated\":%s,\"error\":\"requested affinity could not be confirmed before startup; child was never resumed\"}",pid,terminated?"true":"false");break;}
                char mask[32];sprintf_s(mask,"0x%llX",(unsigned long long)launchOptions.affinityMask);
                cwdFields+=",\"cpu_affinity\":\""+std::string(mask)+"\",\"cpu_affinity_applies_to_spawned_pid\":"+std::to_string(pid);
            }
            if (early) {
                // Suspended: vinput's hooks (and its DLL-load notification for vulkan-1/opengl32) are installed
                // before the game's first instruction; the pipe is up before the main thread is resumed.
                DWORD ti = GetTickCount();
                const int earlyResult=Inject(pid,dll,true,true,&earlyInjectReply);injectReply=earlyInjectReply;
                if (earlyResult != 0) {
                    TerminateProcess(pi.hProcess, 1);   // our own suspended child, by handle
                    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                    if (!g_acLastRefusal.empty()) { printf("%s\n", g_acLastRefusal.c_str()); rc = 4; break; }
                    launchResult("{\"ok\":false,\"pid\":%lu,\"error\":\"early inject into the suspended process failed; process terminated\"}\n", pid); break;
                }
                earlyInjected = true;
                preResumeGuardPid=pid;
                g_acSuspended = false;
                if(!CliBudget(1)||ResumeThread(pi.hThread)==(DWORD)-1){const auto error=CliBudget(1)?GetLastError():ERROR_TIMEOUT;const bool requested=TerminateProcess(pi.hProcess,1)!=FALSE;const bool terminated=requested&&WaitForSingleObject(pi.hProcess,1500)==WAIT_OBJECT_0;CloseHandle(pi.hThread);CloseHandle(pi.hProcess);launchResult("{\"ok\":false,\"pid\":%lu,\"injected\":true,\"suspended_child_termination_requested\":%s,\"suspended_child_terminated\":%s,\"win32_error\":%lu,\"error\":\"early hooks loaded but main-thread resume was refused or launch deadline expired; inspect owned suspended-child cleanup\"}",pid,requested?"true":"false",terminated?"true":"false",error);break;}
                fprintf(stderr, "[vinput] early inject ok in %lu ms, main thread resumed\n", GetTickCount() - ti);
            }
            else if(suspended&&(!CliBudget(1)||ResumeThread(pi.hThread)==(DWORD)-1)){const auto error=CliBudget(1)?GetLastError():ERROR_TIMEOUT;const bool requested=TerminateProcess(pi.hProcess,1)!=FALSE;const bool terminated=requested&&WaitForSingleObject(pi.hProcess,1500)==WAIT_OBJECT_0;CloseHandle(pi.hThread);CloseHandle(pi.hProcess);launchResult("{\"ok\":false,\"pid\":%lu,\"suspended_child_termination_requested\":%s,\"suspended_child_terminated\":%s,\"win32_error\":%lu,\"error\":\"owned child resume was refused or launch deadline expired; inspect suspended-child cleanup\"}",pid,requested?"true":"false",terminated?"true":"false",error);break;}
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        } else if(followPid) {
            pid=followPid;g_initialLaunchPid=pid;g_launchPid=pid;
            rootProcess=LaunchProcess(pid,0,ProcName(pid));processHandles.Retain(rootProcess);
            const auto stampText=GetOpt(a,"follow-created");char* end=nullptr;const auto stamp=strtoull(stampText.c_str(),&end,10);
            if(!stampText.empty()&&(!stamp||!end||*end||(rootProcess.born&&rootProcess.born!=stamp))){launchResult("{\"ok\":false,\"pid\":%lu,\"error\":\"launcher identity changed; follow-created must match its launch receipt\"}",pid);break;}
            if(!rootProcess.born&&stamp)rootProcess.born=stamp;
            if(!rootProcess.born){launchResult("{\"ok\":false,\"pid\":%lu,\"error\":\"launcher exited; pass --follow-created from its launch receipt and --wait-exe; no process was started\"}",pid);break;}
            if(!rootProcess.alive&&waitExe.empty()){launchResult("{\"ok\":false,\"pid\":%lu,\"error\":\"an exited launcher requires --wait-exe to identify its game\"}",pid);break;}
        } else if (!aumid.empty()) {
            // UWP / Xbox app: the activation manager returns the exact PID (no by-name guessing)
            HRESULT ci = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            IApplicationActivationManager* am = nullptr; DWORD p = 0;
            HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&am));
            if (SUCCEEDED(hr)) { hr = am->ActivateApplication(aumid.c_str(), args.empty() ? nullptr : args.c_str(), AO_NONE, &p); am->Release(); }
            if (SUCCEEDED(ci)) CoUninitialize();
            if (FAILED(hr) || !p) { launchResult("{\"ok\":false,\"error\":\"ActivateApplication(%s) failed hr=0x%08X\"}\n", JEsc(W2U(aumid)).c_str(), (unsigned)hr); break; }
            pid = p; g_initialLaunchPid = pid; g_launchPid = pid;
            rootProcess=LaunchProcess(pid,0,ProcName(pid));processHandles.Retain(rootProcess);
        } else {
            // steam://run with args can pop a "launch with these arguments?" confirmation; rungameid never does
            std::wstring url = args.empty() ? L"steam://rungameid/" + steam : L"steam://run/" + steam + L"//" + args + L"/";
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        fprintf(stderr, "[vinput] started (pid %lu)\n", pid);
        DWORD t0 = GetTickCount();
        HWND w = nullptr;
        vi_launch_follow::Tree tree(rootProcess);
        DWORD selectedPid=0,stableSince=0;unsigned long long selectedBorn=0;
        bool ambiguous=false;
        std::map<DWORD,unsigned long long> hiddenProbed;ULONGLONG hiddenObservedAt=0,hiddenAmbiguousAt=0;bool hiddenSeen=false;
        const auto launcherName=vi_launch_follow::Launcher(waitExe);
        // All discovery shares one deadline. Explicit wait-exe permits a fresh
        // Steam-relaunched sibling; ordinary launches only follow proven ancestry.
        while(GetTickCount()-t0<(DWORD)timeout*1000&&CliBudget(1)){
            auto rows=LaunchSnapshot(&tree,waitExe);processHandles.Refresh(tree);
            if(!waitExe.empty())for(const auto& p:rows){
                bool existed=false;for(const auto& prior:initialProcesses)if(prior.pid==p.pid&&prior.born==p.born){existed=true;break;}
                if((!existed||followPid)&&p.born>=(followPid?rootProcess.born:LaunchStamp(launchTime))&&(vi_launch_follow::Name(p.name,waitExe)||(!launcherName.empty()&&vi_launch_follow::Name(p.name,launcherName)))){
                    tree.Remember(p);
                    if(!rootProcess.pid&&!launcherName.empty()&&vi_launch_follow::Name(p.name,launcherName)){rootProcess=p;tree.root=p;g_initialLaunchPid=p.pid;}
                }
            }
            tree.Observe(rows);
            for(auto& p:rows)if(tree.Contains(p)){processHandles.Retain(p);p.window=VisibleWindowOf(p.pid)!=nullptr;}
            auto candidates=tree.Candidates(rows,waitExe);
            bool launcher=false;
            if(candidates.empty()&&!launcherName.empty()) {candidates=tree.Candidates(rows,launcherName);launcher=!candidates.empty();}
            bool hiddenCandidates=false;
            if(candidates.empty()){
                auto hiddenRows=rows;
                for(auto& p:hiddenRows){p.window=false;if(p.alive&&tree.Contains(p)){
                    const auto observed=vi_launch_hidden::Observe(processHandles.Find(p.pid,p.born),p.pid,p.born);
                    if(observed.process_matched&&observed.complete&&observed.hidden_count)p.window=true;
                }}
                candidates=tree.Candidates(hiddenRows,waitExe);
                if(candidates.empty()&&!launcherName.empty()){candidates=tree.Candidates(hiddenRows,launcherName);launcher=!candidates.empty();}
                hiddenCandidates=!candidates.empty();
            }
            ambiguous=candidates.size()>1;
            if(candidates.size()==1){const auto& p=candidates[0];
                if(selectedPid!=p.pid||selectedBorn!=p.born){selectedPid=p.pid;selectedBorn=p.born;stableSince=GetTickCount();hiddenObservedAt=0;hiddenSeen=false;windowHiddenKnown=false;}
                hiddenAmbiguousAt=0;
                // Give a visible stub time to spawn its child; a named final
                // executable already expresses the caller's selection.
                if(GetTickCount()-stableSince>=(waitExe.empty()?750u:250u)){
                    pid=p.pid;selectedCreated=p.born;g_launchPid=pid;w=VisibleWindowOf(pid);
                    if(w&&!allowDialog&&pid!=launcherPid&&!launcher&&PendingStartupSplash(w,pid)){startupSplashSeen=true;w=nullptr;}
                    if(!w&&hiddenCandidates){
                        HANDLE retained=processHandles.Find(pid,p.born);const auto observed=vi_launch_hidden::Observe(retained,pid,p.born);
                        if(!observed.process_matched||!observed.complete||!observed.hidden_count){hiddenObservedAt=0;hiddenSeen=false;windowHiddenKnown=false;continue;}
                        if(!hiddenObservedAt)hiddenObservedAt=GetTickCount64();hiddenSeen=true;windowHiddenKnown=true;windowHidden=true;
                        if(!HasOpt(a,"no-inject")&&hiddenProbed[pid]!=p.born){
                            hiddenProbed[pid]=p.born;
                            if(!(earlyInjected&&pid==rootProcess.pid)){
                                const int probe=Inject(pid,dll,true,true,&hiddenProbeInject);injectReply=hiddenProbeInject;
                                if(probe!=0){hiddenReason="hidden_hooks_only_injection_unconfirmed";break;}
                            }
                        }
                        std::string state;
                        if(ReadinessTransport(pid,"{\"cmd\":\"window-status\"}",500,state,retained)){
                            const auto native=vi_launch_hidden::Parse(state,retained,pid,p.born);
                            if(native.valid&&native.adopted&&native.hidden_known&&native.hidden&&native.presentation_proven){w=native.window;windowAdopted=true;hiddenReason="presenting_hidden_window_adopted";}
                        }
                        if(!w&&GetTickCount64()-hiddenObservedAt>=3000)hiddenReason=HasOpt(a,"no-inject")?"hidden_window_still_starting_no_injection_requested":"hidden_window_still_starting_presenting_proof_unconfirmed";
                    }
                    if(w){if(rootProcess.pid&&pid!=rootProcess.pid)followedFrom=rootProcess.pid;
                        if(IsWindowVisible(w)){windowHiddenKnown=true;windowHidden=false;}
                        if(launcher){launcherPid=pid;waitingFor=W2U(waitExe);}break;}
                }
            }else{selectedPid=0;selectedBorn=0;hiddenObservedAt=0;hiddenSeen=false;windowHiddenKnown=false;
                if(ambiguous&&hiddenCandidates){if(!hiddenAmbiguousAt)hiddenAmbiguousAt=GetTickCount64();if(GetTickCount64()-hiddenAmbiguousAt>=3000){hiddenReason="multiple_hidden_processes_matched";break;}}else hiddenAmbiguousAt=0;
            }
            Sleep(CliBudget(100));
        }
        if(!w){
            if(windowHiddenKnown){const auto current=vi_launch_hidden::Observe(processHandles.Find(pid,selectedCreated),pid,selectedCreated);if(!current.process_matched||!current.complete||!current.hidden_count)windowHiddenKnown=false;}
            const auto loaded=vi_launch_injection::Observe(pid,injectReply);const char* injected=loaded.injectedKnown?(loaded.injected?"true":"false"):earlyInjected?"true":"null";
            launchResult("{\"ok\":false,\"pid\":%lu,\"injected\":%s,\"ready\":false,\"keep_pid\":true,\"still_starting\":%s,\"error\":\"%s\",\"waiting_for_exe\":\"%s\"}",pid,injected,(hiddenSeen||startupSplashSeen)?"true":"false",startupSplashSeen?"startup splash observed; real game window not selected within launch budget; follow this retained PID instead of relaunching":hiddenSeen?"hidden window still starting at launch budget expiry; presenting adoption is unconfirmed; inspect this PID and follow it instead of relaunching":ambiguous?"multiple fresh game windows matched; target selection is unconfirmed; inspect retained PID":"no matching game window within launch timeout; existing processes were left running",JEsc(W2U(waitExe)).c_str());break;
        }
        // Early injection applies only to the original suspended process.
        earlyInjected=earlyInjected&&pid==rootProcess.pid;
        if(rejectDialog(w,earlyInjected))break;
        fprintf(stderr, "[vinput] window up after %.1f s\n", (GetTickCount() - t0) / 1000.0);
        DWORD guardMs = (DWORD)(guardSec * 1000);
        if (HasOpt(a, "no-inject")) {
            GuardForeground(0, "window");
            const auto guardBegan=GetTickCount64();
            GuardForeground(CliBudget(guardMs), "guard");
            guardAppliedMs=(DWORD)(GetTickCount64()-guardBegan);
            bool restored = !TargetHoldsForeground(GetForegroundWindow()),human=LaunchUserTookForeground(); rc = (restored || human) ? 0 : 2;
            launchResult("{\"ok\":%s,\"pid\":%lu,\"injected\":false,\"foreground_restored\":%s,\"user_took_foreground\":%s,\"user_selection_pending\":%s,\"fg_guard_ms\":%lu,\"foreground_regrabs\":%s%s%s}\n", (restored || human) ? "true":"false", pid, restored ? "true":"false",human?"true":"false",g_launchUserIntent.EvaluatingAltTab()?"true":"false", guardMs, RegrabsJson().c_str(),WindowFields(w).c_str(),cwdFields.c_str());
            break;
        }
        InjectChildHandle launchTarget;launchTarget.Reset(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
        HANDLE injectionTarget=launchTarget.value;
        const bool exactDialogTarget=(allowDialog||launcherPid==pid)&&(WindowClass(w)=="#32770"||OwnedPopupWindow(w));
        int injectionResult = Inject(pid,dll,true,exactDialogTarget,&injectReply);
        if(injectionResult==0&&exactDialogTarget) {
            dialogInjectReply=injectReply;
            // The core deliberately does not auto-adopt owned windows. Only an
            // explicit allow-dialog request may select this exact target HWND.
            // Configure while responsive, then demand the normal fresh queue
            // handshake; hooks-only never becomes the final launch success.
            char handle[32];sprintf_s(handle,"0x%llX",(unsigned long long)(ULONG_PTR)w);
            std::string configured;
            const std::string command="{\"cmd\":\"config\",\"hwnd\":\""+std::string(handle)+"\"}";
            const bool received=ReadinessTransport(pid,command,5000,configured,injectionTarget);
            vi_ready_json::Reader parsed{configured,0,{},128};bool accepted=false;std::string target;
            const bool valid=received&&parsed.Parse()&&parsed.Bool("ok",accepted)&&accepted;
            auto field=parsed.fields.find("hwnd");
            if(valid&&field!=parsed.fields.end()&&field->second.type=='"') {
                vi_ready_json::Reader decoded{field->second.raw,0,{}};if(!decoded.String(target))target.clear();
            }
            if(!valid||target!=handle) {
                fprintf(stderr,"[vinput] explicit dialog target adoption failed; no full readiness was established\n");
                injectionResult=2;
            }else injectionResult=ReportInputReady(stderr,pid,dll,true,false,true,injectionTarget,&injectReply);
        }
        bool liveReadinessUnconfirmed=false;
        if(injectionResult!=0&&g_acLastRefusal.empty()){
            const auto loaded=vi_launch_injection::Observe(pid,injectReply);vi_ready_json::Reader parsed{injectReply,0,{}};
            if(loaded.injectedKnown&&loaded.injected&&parsed.Parse()){
                auto nested=parsed.fields.find("readiness");
                if(nested!=parsed.fields.end()&&nested->second.type=='{'&&LaunchLiveWindow(nested->second.raw,pid,w,injectionTarget,selectedCreated)){
                    launchReadiness=nested->second.raw;liveReadinessUnconfirmed=true;
                }
            }
        }
        if (injectionResult != 0&&!liveReadinessUnconfirmed) {
            // launchTarget (InjectChildHandle) owns injectionTarget and closes it.
            if (!g_acLastRefusal.empty()) { printf("%s\n", g_acLastRefusal.c_str()); rc = 4; break; }
            std::string exitedReply;
            if(PrintInjectionExit(nullptr,injectionTarget,pid,"inject",0,&exitedReply))launchResult("%s",exitedReply.c_str());
            else {const auto failure=vi_launch_injection::Failure(pid,injectReply);launchResult("%s",failure.c_str());}
            break;
        }
        windowAdopted=true;windowHiddenKnown=true;windowHidden=IsWindowVisible(w)==FALSE;
        showHiddenConfirmed=showHidden&&!windowHidden;
        // wait until vinput has adopted a main window, then let the game settle before giving up the lock;
        // a foreground grab during the settle is handed back at once instead of after it
        bool adoptionProven=liveReadinessUnconfirmed;
        {vi_ready_json::Reader outer{injectReply,0,{}};if(outer.Parse()){auto nested=outer.fields.find("readiness");HWND actual=nullptr;bool valid=false;if(nested!=outer.fields.end()&&nested->second.type=='{'&&LaunchRetainedProcess(pid,injectionTarget,selectedCreated)&&LaunchReadinessTarget(nested->second.raw,pid,w,actual,valid))adoptionProven=true;}}
        std::string resp; DWORD t1 = GetTickCount();
        while (!adoptionProven&&GetTickCount() - t1 < 60000&&optionalBudget(1)) {
            if(Request(pid,"{\"cmd\":\"status\"}",resp,optionalBudget(2000))){
                vi_ready_json::Reader status{resp,0,{},128};bool accepted=false;
                if(status.Parse()&&status.Bool("ok",accepted)&&accepted){auto handle=status.fields.find("hwnd");if(handle!=status.fields.end()&&handle->second.type=='"'&&handle->second.raw!="\"0x0\"")break;}
            }
            GuardForeground(0,"adopt");Sleep(optionalBudget(500));
        }
        std::string claim;bool claimed=g_owner.empty();
        auto takeClaim=[&](DWORD timeout){
            if(g_owner.empty())return true;
            std::string cr;const bool received=Request(pid, "{\"cmd\":\"claim\",\"ttl\":" + claimTtl + "}", cr, timeout,injectionTarget);vi_ready_json::Reader parsed{cr,0,{}};bool accepted=false;claimed=received&&parsed.Parse()&&parsed.Bool("ok",accepted)&&accepted;
            if(claimed){auto state=parsed.fields.find("claim");if(state==parsed.fields.end()||state->second.type!='{')claimed=false;else{vi_ready_json::Reader owner{state->second.raw,0,{}};bool active=false;std::string name;claimed=owner.Parse()&&owner.Bool("active",active)&&active&&vi_launch_hidden::String(owner,"owner",name)&&name==g_owner;}}
            claim = ",\"claimed\":" + std::string(claimed ? "true" : "false") + ",\"owner\":\"" + JEsc(g_owner) + "\"";
            return claimed;
        };
        if(showHidden&&windowAdopted&&windowHidden&&optionalBudget(1)&&takeClaim(optionalBudget(500))){
            char handle[32];sprintf_s(handle,"0x%llX",(unsigned long long)(ULONG_PTR)w);std::string response;
            if(ReadinessTransport(pid,"{\"cmd\":\"show-hidden\",\"hwnd\":\""+std::string(handle)+"\"}",optionalBudget(1000),response,injectionTarget)){
                vi_ready_json::Reader initial{response,0,{}};bool accepted=false;unsigned long long version=0,ticket=0;
                if(initial.Parse()){showHiddenReply=response;auto state=initial.fields.find("hidden_show");
                    if(initial.Bool("ok",accepted)&&accepted&&vi_launch_hidden::Uint(initial,"show_hidden_version",1,version)&&version==1&&state!=initial.fields.end()&&state->second.type=='{'){
                        vi_ready_json::Reader receipt{state->second.raw,0,{}};std::string receiptWindow;unsigned long long adoption=0;
                        if(receipt.Parse()&&vi_launch_hidden::Uint(receipt,"ticket",(std::numeric_limits<unsigned long long>::max)(),ticket,true)&&vi_launch_hidden::String(receipt,"hwnd",receiptWindow)&&receiptWindow==handle&&vi_launch_hidden::Uint(receipt,"adoption_generation",(std::numeric_limits<unsigned long long>::max)(),adoption,true)){
                            const auto until=GetTickCount64()+optionalBudget(1000);
                            do{
                                const auto remaining=vi_cli_deadline::Remaining(until,GetTickCount64(),250);if(!remaining)break;
                                std::string current;if(!ReadinessTransport(pid,"{\"cmd\":\"presentation-status\"}",remaining,current,injectionTarget))break;
                                const auto native=vi_launch_hidden::Parse(current,injectionTarget,pid,selectedCreated);vi_ready_json::Reader status{current,0,{}};
                                if(!native.valid||!native.adopted||native.window!=w||!status.Parse())break;
                                auto item=status.fields.find("hidden_show");if(item==status.fields.end()||item->second.type!='{')break;
                                vi_ready_json::Reader seen{item->second.raw,0,{}};unsigned long long actual=0,actualAdoption=0;std::string actualWindow;bool pending=false,completed=false;
                                if(!seen.Parse()||!vi_launch_hidden::Uint(seen,"ticket",(std::numeric_limits<unsigned long long>::max)(),actual,true)||actual!=ticket||!vi_launch_hidden::String(seen,"hwnd",actualWindow)||actualWindow!=receiptWindow||!vi_launch_hidden::Uint(seen,"adoption_generation",(std::numeric_limits<unsigned long long>::max)(),actualAdoption,true)||actualAdoption!=adoption||!seen.Bool("pending",pending)||!seen.Bool("completed",completed))break;
                                showHiddenStatus=current;windowHiddenKnown=native.hidden_known;windowHidden=native.hidden;
                                showHiddenConfirmed=native.hidden_known&&!native.hidden;
                                if(completed||!pending)break;
                                Sleep(CliBudget(vi_cli_deadline::Remaining(until,GetTickCount64(),20)));
                            }while(GetTickCount64()<until&&CliBudget(1));
                        }
                    }
                }
            }
        }
        const auto settleBegan=GetTickCount64();GuardForeground(optionalBudget(settleRequestedMs),"settle");settleAppliedMs=(DWORD)(GetTickCount64()-settleBegan);
        const auto guardBegan=GetTickCount64();GuardForeground(optionalBudget(guardMs),"guard");guardAppliedMs=(DWORD)(GetTickCount64()-guardBegan);
        bool restored = !TargetHoldsForeground(GetForegroundWindow());
        bool alive = LaunchRetainedProcess(pid,injectionTarget,selectedCreated);
        HWND finalWindow=VisibleWindowOf(pid);if(finalWindow)w=finalWindow;
        if(rejectDialog(w,true))break;
        std::string clientSizeFields;
        if(launchOptions.clientSize){const auto resizeBudget=optionalBudget(2000);if(alive&&!liveReadinessUnconfirmed&&resizeBudget){CliDeadlineScope resizeDeadline(resizeBudget);clientSizeFields=ApplyLaunchClientSize(pid,launchOptions);}else clientSizeFields=",\"client_size_applied\":false,\"client_size_warning\":\"queue or optional launch budget is pending; no resize was requested\"";}
        if (launchOptions.clientSize) {
            alive = LaunchRetainedProcess(pid,injectionTarget,selectedCreated);
            restored = !TargetHoldsForeground(GetForegroundWindow());
        }
        std::string render,renderStatus,renderError;
        if(alive&&!liveReadinessUnconfirmed&&optionalBudget(1)&&ReadinessTransport(pid,"{\"cmd\":\"status\"}",optionalBudget(500),renderStatus,injectionTarget)){
            std::string frames=vi_doctor::Field(renderStatus,"frames"),fs=vi_doctor::Field(renderStatus,"fullscreen");
            bool ready=frames!="null"&&strtod(frames.c_str(),nullptr)>0;
            render=",\"render_ready\":"+std::string(ready?"true":"false");
            if(fs!="null")render+=",\"fullscreen\":"+fs;
            std::string dd=vi_doctor::Field(renderStatus,"ddraw");if(dd!="null")render+=",\"ddraw\":"+dd;
            std::string d9=vi_doctor::Field(renderStatus,"d3d9");if(d9!="null")render+=",\"d3d9\":"+d9;
            if(vi_doctor::Field(fs,"blocked_before_first_present")=="true"||vi_doctor::Field(dd,"blocked_before_first_present")=="true"||vi_doctor::Field(d9,"blocked_before_first_present")=="true")render+=",\"render_warning\":\"repeated fullscreen negotiation failures with no target Present; inspect fullscreen diagnostics; native exclusive is disabled\"";
            else if(vi_doctor::Field(d9,"render_warning")!="null")render+=",\"render_warning\":"+vi_doctor::Field(d9,"render_warning");
        }
        std::string presentationFields=clientSizeFields+render;
        claimed=takeClaim(500);
        // The game is running and injected: a failed hand-back is a warning, not a failure (ok:false invites a
        // duplicate launch). The caller must check foreground_restored and fix it under the lock.
        std::string warn;
        if (!restored && !g_launchUserIntent.Active()) warn = ",\"warning\":\"the target still holds the REAL foreground after the guard; hand it back under the lock (vinput --pid N json {\\\"cmd\\\":\\\"restore-foreground\\\",\\\"hwnd\\\":\\\"0x...\\\"})\"";
        bool ok = alive&&claimed;
        if(ok){
            std::string fresh;HWND actual=nullptr;bool validJson=false;
            const bool received=ReadinessTransport(pid,liveReadinessUnconfirmed?"{\"cmd\":\"readiness\",\"hooks_only\":false,\"wait_ms\":0}":"{\"cmd\":\"readiness\",\"hooks_only\":false,\"wait_ms\":250}",1000,fresh,injectionTarget);
            const bool same=received&&LaunchReadinessTarget(fresh,pid,w,actual,validJson);
            const bool live=received&&LaunchLiveWindow(fresh,pid,w,injectionTarget,selectedCreated);
            liveReadinessUnconfirmed=!same&&live;
            if(validJson)launchReadiness=fresh;
            if(!same&&!live){
                windowAdopted=false;
                char expectedText[32],actualText[32];sprintf_s(expectedText,"0x%llX",(unsigned long long)(ULONG_PTR)w);sprintf_s(actualText,"0x%llX",(unsigned long long)(ULONG_PTR)actual);
                launchResult("{\"ok\":false,\"pid\":%lu,\"injected\":true,\"selection_mismatch\":%s,\"expected_window_hwnd\":\"%s\",\"actual_window_hwnd\":\"%s\",\"error\":\"selected launch window does not have matching full current readiness; inspect both HWNDs; no automatic retarget was attempted\"%s}\n",pid,actual&&actual!=w?"true":"false",expectedText,actualText,WindowFields(w).c_str());break;
            }
            windowAdopted=true;windowHiddenKnown=true;windowHidden=IsWindowVisible(w)==FALSE;showHiddenConfirmed=showHidden&&!windowHidden;
        }
        if(liveReadinessUnconfirmed)warn+=",\"ready\":false,\"input_ready\":false,\"queue_ready\":false,\"readiness_unconfirmed\":true,\"readiness_warning\":\"live adopted window and DLL load confirmed; input queue is not progressing; do not relaunch or replay input automatically\"";
        else warn+=",\"input_ready\":"+std::string(ok?"true":"false")+",\"queue_ready\":"+(ok?"true":"false")+",\"readiness_unconfirmed\":false";
        if(!claimed)warn+=",\"claim_error\":\"live launch claim was not confirmed\"";
        rc = ok ? 0 : 2;
        launchResult("{\"ok\":%s,\"pid\":%lu,\"injected\":true,\"early_injected\":%s,\"foreground_restored\":%s,\"user_took_foreground\":%s,\"fg_guard_ms\":%lu,\"foreground_regrabs\":%s%s%s%s%s%s%s}\n",
               ok ? "true" : "false", pid, earlyInjected ? "true" : "false", restored ? "true" : "false",LaunchUserTookForeground()?"true":"false", guardMs, RegrabsJson().c_str(), claim.c_str(), warn.c_str(),
               ok ? "" : ",\"error\":\"target liveness, pipe or requested claim was not confirmed during settle/guard\"",WindowFields(w).c_str(),cwdFields.c_str(),presentationFields.c_str());
    } while (false);
    watching=false; watcher.join(); RestoreForeground(); ReleaseLock();
    return rc;
}

static int ListPipes() {
    WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW(L"\\\\.\\pipe\\*", &fd);
    printf("[");
    bool first = true;
    if (f != INVALID_HANDLE_VALUE) {
        do {
            const wchar_t* leaf = wcsrchr(fd.cFileName, L'\\'); leaf = leaf ? leaf + 1 : fd.cFileName;   // AppContainer pipes carry a namespace prefix
            if (!wcsncmp(leaf, L"vinput_", 7)) {
                DWORD pid = wcstoul(leaf + 7, nullptr, 10);
                // owner / uptime / real foreground from the target itself (status is an open command: never refused)
                std::string st, owner, extra;
                if (Request(pid, "{\"cmd\":\"status\"}", st, 300) && st.find("\"ok\":true") != std::string::npos) {
                    size_t c = st.find("\"claim\":{");
                    std::string claimObj = c == std::string::npos ? std::string() : st.substr(c, st.find('}', c) - c + 1);
                    owner = JsonText(claimObj, "owner");
                    auto raw = [&](const std::string& src, const char* k) { std::string m = "\"" + std::string(k) + "\":"; size_t p = src.find(m); if (p == std::string::npos) return std::string("null"); p += m.size(); size_t e = src.find_first_of(",}", p); return src.substr(p, e - p); };
                    extra = ",\"owner\":" + (owner.empty() ? std::string("null") : "\"" + JEsc(owner) + "\"") + ",\"claim_expires_in_ms\":" + raw(claimObj, "expires_in_ms") +
                            ",\"uptime_ms\":" + raw(st, "uptime_ms") + ",\"really_foreground\":" + raw(st, "really_foreground") + ",\"responding\":true";
                } else extra = ",\"responding\":false";
                printf("%s{\"pid\":%lu,\"exe\":\"%s\"%s}", first ? "" : ",", pid, JEsc(W2U(ProcName(pid))).c_str(), extra.c_str()); first = false;
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    printf("]\n"); return 0;
}

#include "vi_cli_escape_host.inl"

static void Usage() {
    const char* usage =
        "vinput - per-process virtual input + capture for automating offline games\n"
        "  vinput inject --pid N [--dll PATH] [--hooks-only[=true|false]]\n"
        "      inject waits for keyboard/focus hooks, an adopted window and posted queue progress; --hooks-only is for suspended/no-window setup.\n"
        "      --hooks-only=false retains the full handshake. Input commands still wait for a ready window/queue.\n"
        "  vinput launch (--exe PATH | --steam APPID --wait-exe NAME.exe | --aumid PFN!AppId) [--args \"...\"] [--wait-exe NAME.exe]\n"
        "                [--lock PATH] [--lock-retry 60] [--lock-timeout SECONDS] [--timeout 240] [--settle 5] [--fg-guard 5] [--early] [--dll PATH] [--no-inject] [--claim-ttl 300] [--show-hidden]\n"
        "      --lock-retry: seconds BETWEEN attempts (1..86400); --lock-timeout: total wait seconds (0..86400, default0=unlimited).\n"
        "      Existing --lock-max-wait is an alias for --lock-timeout. Sleeps are capped by its remaining budget; native calls/scheduling may overrun.\n"
        "      Direct --exe: --cwd DIRECTORY (existing child working directory; relative exe still resolves from caller).\n"
        "      Direct injected --exe: --client-size WIDTHxHEIGHT (1..8192 each); verified one-shot after settle, no activation/movement.\n"
        "      Inspect client_size.applied/warning; a running game remains ok:true on size refusal. Hosted/CoreWindow sizing is unsupported.\n"
        "                --early: create suspended, inject, resume (needs --exe; covers Vulkan/GL device creation)\n"
        "                Hidden presenting windows are adopted without showing. --show-hidden explicitly queues one SW_SHOWNA; inspect the ticket/visibility receipt, not just ok. Default false.\n"
        "                --fg-guard S: keep handing the real foreground back for S s after the restore (0 = off)\n"
        "  vinput list                                  pids with a live pipe: exe, owner, uptime, really_foreground\n"
        "  vinput doctor --pid N [--key KEY]             read-only architecture, input/capture, foreground, claim and log diagnostics\n"
        "  vinput [--owner NAME] [--force-claim] --pid N <command>   (or env VINPUT_OWNER; launch --owner claims the new pid)\n"
        "  vinput --owner NAME --pid N claim [--ttl 300] | unclaim     other owners are refused until unclaim/expiry\n"
        "  vinput --pid N status | ping | log | activate | release | restore\n"
        "  vinput --pid N waitexit [--timeout MS]       block until the process exits; reply has exit_code (works on any pid, injected or not)\n"
        "  vinput --pid N readiness [--wait-ms 1000] [--hooks-only[=true|false]]\n"
        "      readiness reports hook_ready/window_ready/queue_ready; host/application startup is unmeasured. wait-ms is0..10000.\n"
        "  vinput --pid N jobs | job-status ID | job-cancel ID; timed key/hold/pad accept --async [--replace]\n"
        "  vinput --pid N seq <file>          keyboard sequence returns a background job ID; use job-status/job-cancel\n"
        "  vinput --pid N pulse <key> <ms> --duty 25 --period-ms 100 [--async]\n"
        "  vinput --pid N pad-ramp lx 0 16000 1000 [--pacing frames|time] [--end-hold-ms 100]\n"
        "  vinput --pid N pad-timeline states.json [--pacing frames|time] [--strict-frames] [--stagger-ms 60]\n"
        "  vinput --pid N hold-while w --until-text TEXT [--timeout-ms 5000] (pad-hold-while also available)\n"
        "  vinput --pid N key <combo> [ms] [--count N] [--gap ms]      combo: enter, esc, w, ctrl+s, f1, 0x2D ...\n"
        "  vinput --pid N down <combo> | up <combo> | hold <combo> <ms>\n"
        "    key/hold: --expect-text \"PAUSE MENU\" [--expect-timeout-ms 3000] [--expect-region x,y,w,h]\n"
        "      Inject once, then observe fresh text. Failure is non-zero; delivery/API reads do not prove consumption.\n"
        "  vinput --pid N type \"text\" [--delay ms] [--keys]\n"
        "  vinput --pid N mouse <x> <y> [--norm] [--capture-space] [--steps N]   (client pixels)\n"
        "  vinput --pid N move <dx> <dy> [--steps N]                            (relative)\n"
        "  vinput --pid N click [left|right|middle] [<x> <y>] [--norm] [--capture-space] [--count N] [--ms 60]\n"
        "  vinput --pid N wheel <delta>\n"
        "  vinput --pid N pad <a+b+lt+rt+start...|none> [ms] [--lx N --ly N --rx N --ry N --lt N --rt N] [--hold]\n"
        "  vinput --pid N capture <out.png> [--method auto|swapchain|printwindow]\n"
        "      [--scale 0.05..4 | --max-width 640] [--format png|jpeg] [--quality 85] (JPEG only); source hash stays before output transforms.\n"
        "  vinput --pid N capture-burst <sheet.png> [--frames 8] [--interval-ms 250] [--columns 4] [--max-width 320] [--format png|jpeg] [--quiet[=true|false]]\n"
        "      Bounded temporal tiles; actual timing metadata, other commands run between frames; never changes --capture-space.\n"
        "  vinput --pid N config isolate=false --for 1000   (1..120000 ms; physical input passes until auto-revert)\n"
        "  vinput --pid N config fakefocus=true autoactivate=true isolate=true blockfocus=true keepvisible=true cursor=auto|virtual|real raw=auto|post|buffer|off msgs=auto|on|off [for_ms=1..120000 | --for N (isolate=false only)]\n"
        "  vinput --pid N json '{\"cmd\":\"status\"}'\n"
        "  vinput --pid N ocr [--region x,y,w,h] [--lang en-US] [--scale 1] [--preprocess MODE] [--retry-empty=true|false] [--text-only] [--lines-only] [--out file.json]\n"
        "  vinput --pid N click-text \"text\" [--nth N] [--exact] [--whole-word=true|false] [--exact-line=true|false] [--fuzzy N] [--timeout ms] [--button left] [--dry-run] [--optional]\n"  //@text
        "  vinput --pid N wait-text \"text\" [--timeout ms] [--gone]        vinput --pid N assert-text \"text\" [--absent]\n"  //@text
        "  vinput --pid N wait-text --any \"a|b\" [--timeout ms] [--gone]\n"
        "  Text search: --preprocess auto|none|gray|contrast|threshold|invert; misses include actual nearest_candidates (suggestions only).\n"
        "  vinput --pid N find-image|click-image|wait-image <template.png> [--threshold 0.8] [--scales 0.9,1,1.1|--no-scale] [--region ..] [--nth N] [--timeout ms] [--gone]\n"  //@image
        "      find-image --preserve-map retains the saved single-image cursor map and reports source capture scope.\n"
        "  vinput --pid N pixel <x> <y>       vinput --pid N scroll <N> [x y]       vinput --pid N drag x1 y1 x2 y2 [--ms 300]\n"  //@small
        "  vinput --pid N placement-drag x1 y1 x2 y2 [--steps 15 --ms 300 --dwell-ms 900 --poll-timeout-ms 1000 --async]\n"
        "      Owned polled placement; drag --placement is equivalent. config edge_margin=8 guards pointer paths (0 disables).\n"
        "      click --di-poll-timeout-ms N requires down/up reads; --di-release-timeout-ms N sets a separate up cap.\n"
        "  vinput --pid N script <file>       one command per line (any command above), '#' comments, sleep <ms>, echo <text>; stops at the first failing line (--continue to go on)\n"  //@script
        "  vinput --pid N escape-host [--timeout-ms N]  opt-in Ctrl+Alt+Shift+End host; streams receipts; no automatic start\n"
        "  vinput --pid N foreground          VERIFY/HANDOFF ONLY: really bring the game to the front\n"
        "  vinput sysinput <vk>               VERIFY ONLY: OS-level SendInput key (= the shared physical keyboard)\n";
    if (g_jsonOutput) printf("{\"ok\":true,\"text_word_boundary_version\":1,\"text_line_match_version\":1,\"ocr_empty_retry_version\":1,\"ocr_compact_version\":1,\"image_scope_version\":1,\"help\":\"%s\"}\n", JEsc(usage).c_str()); else printf("%s", usage);
}

int wmain(int argc, wchar_t** wargv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> av; for (int i = 1; i < argc; i++) av.push_back(W2U(wargv[i]));
    for (size_t i=0; i<av.size();) {
        if (av[i] == "--json") { g_jsonOutput=true; av.erase(av.begin()+i); }
        else if (av[i] == "--args" || av[i] == "--dll" || av[i] == "--exe") i += (i+1<av.size() ? 2 : 1);
        else ++i;
    }
    if (av.empty() || av[0] == "-h" || av[0] == "--help" || av[0] == "help") {
        if (!av.empty() && HasExpectationOptions(SplitArgs(std::vector<std::string>(av.begin() + 1, av.end())))) {
            Reply r = ExpectRejected("expectation flags apply only to key and hold; no input was injected"); printf("%s\n",r.json.c_str());return 2;
        }
        Usage(); return av.empty() ? 1 : 0;
    }
    DWORD pid = 0;
    std::vector<std::string> rest;
    { wchar_t envOwner[256] = {0}; if (GetEnvironmentVariableW(L"VINPUT_OWNER", envOwner, 255)) g_owner = W2U(envOwner); }
    for (size_t i = 0; i < av.size(); i++) {
        if (av[i] == "--pid" && i + 1 < av.size()) { pid = strtoul(av[++i].c_str(), nullptr, 0); continue; }
        if (av[i] == "--owner" && i + 1 < av.size()) { g_owner = av[++i]; continue; }
        if (av[i] == "--force-claim") { g_forceClaim = true; continue; }
        if ((av[i] == "--args" || av[i] == "--dll" || av[i] == "--exe") && i + 1 < av.size()) { rest.push_back(av[i]); rest.push_back(av[++i]); continue; }
        rest.push_back(av[i]);
    }
    if (rest.empty()) { Usage(); return 1; }
    const std::string cmd = rest[0];
    Args a = SplitArgs(std::vector<std::string>(rest.begin() + 1, rest.end()),cmd=="capture-burst");
    if(cmd=="escape-host")return vi_escape_cli::Run(av);
    if((HasOpt(a,"for")||HasOpt(a,"for-ms")||HasOpt(a,"for_ms"))&&cmd!="config"){printf("{\"ok\":false,\"configuration_changed\":false,\"error\":\"--for applies only to config isolate=false\"}\n");return 2;}
    if(HasOpt(a,"stagger-ms")&&cmd!="pad-timeline"){printf("{\"ok\":false,\"input_injected\":false,\"error\":\"--stagger-ms is only implemented for pad-timeline\"}\n");return 2;}
    if (HasExpectationOptions(a) && cmd != "key" && cmd != "hold") {
        Reply r = ExpectRejected("expectation flags apply only to key and hold; no input was injected"); printf("%s\n",r.json.c_str()); return 2;
    }
    if (cmd == "inject") {
        bool hooksOnly=false;std::string flagError;
        if(!InjectHooksOnly(a,hooksOnly,flagError)){printf("{\"ok\":false,\"input_injected\":false,\"error\":\"%s\"}\n",JEsc(flagError).c_str());return 2;}
        if (!pid) { printf("{\"ok\":false,\"error\":\"inject needs --pid\"}\n"); return 2; }
        if(HasOpt(a,"readiness-wait-ms")){const auto text=GetOpt(a,"readiness-wait-ms");char* end=nullptr;const auto value=strtoul(text.c_str(),&end,10);if(text.empty()||!end||*end||value<1||value>5000){printf("{\"ok\":false,\"error\":\"readiness-wait-ms must be1..5000; no injection was requested\"}\n");return 2;}g_cliReadinessWaitMs=(DWORD)value;}
        g_acSuspended = HasOpt(a, "internal-suspended");   // passed only by vinput.exe to vinput32.exe for launch --early
        return Inject(pid, U2W(GetOpt(a, "dll")), HasOpt(a, "quiet"),hooksOnly);
    }
    if (cmd == "launch") return Launch(a);
    if (cmd == "list") return ListPipes();
    if (cmd == "doctor") { Reply r = CmdDoctor(pid, a); printf("%s\n", r.json.c_str()); return r.ok ? 0 : 1; }
    if (cmd == "keys") {
        printf("{\"ok\":true,\"keys\":["); bool first = true;
        for (const auto& k : vi_keys::Names()) {
            printf("%s{\"name\":\"%s\",\"vk\":%d}", first ? "" : ",", JEsc(k.name).c_str(), k.vk); first = false;
        }
        printf("]}\n"); return 0;
    }
    if (cmd == "sysinput") {
        // VERIFY-ONLY: OS-level SendInput into the real input stream (what a physical key produces; goes to
        // whatever window is REALLY foreground). Never use this to drive a game - it is the shared keyboard.
        if (a.pos.empty()) { printf("{\"ok\":false,\"error\":\"sysinput needs a vk (e.g. 0x0D)\"}\n"); return 2; }
        WORD vk = (WORD)strtoul(a.pos[0].c_str(), nullptr, 0);
        INPUT in[2] = {}; in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = vk; in[0].ki.wScan = in[1].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        UINT n1 = SendInput(1, &in[0], sizeof(INPUT)); Sleep(80); UINT n2 = SendInput(1, &in[1], sizeof(INPUT));
        printf("{\"ok\":%s,\"vk\":%u}\n", (n1 && n2) ? "true" : "false", vk); return (n1 && n2) ? 0 : 1;
    }
    if (!pid) { printf("{\"ok\":false,\"error\":\"--pid N required\"}\n"); return 2; }
    if (cmd == "waitexit") {
        // Holds a handle so the exit code is still readable after the process is gone (a pipe error cannot tell us).
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) { printf("{\"ok\":false,\"pid\":%lu,\"exited\":true,\"error\":\"no such process (OpenProcess failed %lu)\"}\n", pid, GetLastError()); return 1; }
        DWORD ms = HasOpt(a, "timeout") ? (DWORD)strtoul(GetOpt(a, "timeout").c_str(), nullptr, 0) : INFINITE;
        DWORD w = WaitForSingleObject(h, ms), code = 0;
        if (w == WAIT_TIMEOUT) { CloseHandle(h); printf("{\"ok\":true,\"pid\":%lu,\"exited\":false}\n", pid); return 0; }
        if (w != WAIT_OBJECT_0 || !GetExitCodeProcess(h, &code)) { CloseHandle(h); printf("{\"ok\":false,\"pid\":%lu,\"error\":\"wait failed %lu\"}\n", pid, GetLastError()); return 1; }
        CloseHandle(h);
        printf("{\"ok\":true,\"pid\":%lu,\"exited\":true,\"exit_code\":%lu,\"exit_code_hex\":\"0x%08lX\"}\n", pid, code, code);
        return 0;
    }
    if (cmd == "foreground") {
        // VERIFY / HANDOFF ONLY: really bring the game's window to the front (steals focus from everyone).
        std::string resp; Request(pid, "{\"cmd\":\"status\"}", resp);
        size_t p = resp.find("\"hwnd\":\""); if (p == std::string::npos) { printf("%s\n", resp.c_str()); return 1; }
        HWND h = (HWND)(ULONG_PTR)strtoull(resp.c_str() + p + 8, nullptr, 0);
        if(!IsWindow(h)) { printf("{\"ok\":false,\"error\":\"target has no valid window for foreground access\"}\n");return 1; }
        HWND root=GetAncestor(h,GA_ROOT);if(root)h=root;
        double lease=atof(GetOpt(a,"lease-ms","30000").c_str());
        if(!(lease>=1 && lease<=300000)) { printf("{\"ok\":false,\"error\":\"--lease-ms must be 1..300000\"}\n");return 2; }
        if(!Request(pid,"{\"cmd\":\"foreground-lease\",\"ms\":"+std::to_string((unsigned long)lease)+"}",resp) || resp.find("\"ok\":true")==std::string::npos) {
            printf("%s\n",resp.c_str());return 1;
        }
        INPUT alt[2] = {}; alt[0].type = alt[1].type = INPUT_KEYBOARD; alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU; alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, alt, sizeof(INPUT));  // the documented "last input event" foreground-lock unlock
        BOOL ok = SetForegroundWindow(h);
        Sleep(300);
        bool fg = GetForegroundWindow()==h || ForegroundIsTarget(GetForegroundWindow(),pid);
        if(!fg) Request(pid,"{\"cmd\":\"restore-foreground\"}",resp); // also cancels a failed attempt's lease
        printf("{\"ok\":%s,\"hwnd\":\"0x%llX\",\"set\":%d,\"lease_ms\":%lu}\n", fg ? "true" : "false", (unsigned long long)(ULONG_PTR)h, ok,(unsigned long)lease); return fg ? 0 : 1;
    }
    Reply r = Dispatch(pid, rest);
    printf("%s\n", r.json.c_str());
    return r.ok ? 0 : 1;
}
