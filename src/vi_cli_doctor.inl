// Read-only, bounded diagnostics in the CLI. No injection, capture, input, claims or foreground changes.
// Included after vi_cli_ui.inl in vinput_cli.cpp (Args/Reply/process helpers already available).
namespace vi_doctor {
static void Space(const std::string& j, size_t& p) {
    while (p < j.size() && (j[p] == ' ' || j[p] == '\r' || j[p] == '\n' || j[p] == '\t')) ++p;
}
static bool SkipString(const std::string& j, size_t& p) {
    if (p >= j.size() || j[p++] != '"') return false;
    while (p < j.size()) { char c = j[p++]; if (c == '"') return true; if (c == '\\') { if (p == j.size()) return false; ++p; } }
    return false;
}
static bool SkipValue(const std::string& j, size_t& p) {
    Space(j, p); if (p >= j.size()) return false;
    if (j[p] == '"') return SkipString(j, p);
    if (j[p] == '{' || j[p] == '[') {
        std::vector<char> close{j[p++] == '{' ? '}' : ']'};
        while (p < j.size() && !close.empty()) {
            if (j[p] == '"') { if (!SkipString(j, p)) return false; }
            else if (j[p] == '{' || j[p] == '[') { if (close.size() >= 32) return false; close.push_back(j[p++] == '{' ? '}' : ']'); }
            else if (j[p] == '}' || j[p] == ']') { if (j[p++] != close.back()) return false; close.pop_back(); }
            else ++p;
        }
        return close.empty();
    }
    size_t start = p;
    while (p < j.size() && j[p] != ',' && j[p] != '}' && j[p] != ']' && j[p] != ' ' && j[p] != '\r' && j[p] != '\n' && j[p] != '\t') ++p;
    return p > start;
}
// Retrieve only a direct property, never a same-named property from an embedded object or string.
static std::string Field(const std::string& j, const std::string& key) {
    size_t p = 0; Space(j, p); if (p >= j.size() || j[p++] != '{') return "null";
    while (p < j.size()) {
        Space(j, p); if (p >= j.size() || j[p] == '}') break;
        size_t k = p; if (!SkipString(j, p)) break;
        bool match = j.substr(k, p-k) == "\"" + key + "\"";
        Space(j, p); if (p >= j.size() || j[p++] != ':') break;
        Space(j, p); size_t v = p; if (!SkipValue(j, p)) break;
        if (match) return j.substr(v, p-v);
        Space(j, p); if (p >= j.size() || j[p++] != ',') break;
    }
    return "null";
}
static std::string String(const std::string& s) { return "\"" + JEsc(s) + "\""; }
static bool Query(DWORD pid, const std::string& command, std::string& reply, std::string& error) {
    HANDLE pipe = OpenPipe(pid, 400);
    if (pipe == INVALID_HANDLE_VALUE) { error = "no reachable vinput pipe (not injected, unavailable, or process gone)"; return false; }
    std::string wire = command + "\n"; DWORD written = 0;
    bool done = false;
    if (!WriteFile(pipe, wire.data(), (DWORD)wire.size(), &written, nullptr) || written != wire.size()) error = "diagnostic request write failed";
    else {
        ULONGLONG until = GetTickCount64() + 1500;
        reply.clear();
        while (GetTickCount64() < until) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) { error = "diagnostic pipe closed before its reply"; break; }
            if (!available) { Sleep(5); continue; }
            char block[4096]; DWORD got = 0;
            if (!ReadFile(pipe, block, (DWORD)(std::min)(available, (DWORD)sizeof(block)), &got, nullptr) || !got) { error = "diagnostic reply read failed"; break; }
            reply.append(block, got);
            if (reply.size() > 65536) { error = "diagnostic reply exceeds 64 KiB"; break; }
            size_t newline = reply.find('\n');
            if (newline != std::string::npos) { reply.resize(newline); done = true; break; }
        }
        if (!done && error.empty()) error = "diagnostic reply timed out (1500 ms); the target may be unresponsive";
    }
    CloseHandle(pipe);
    return done;
}
static std::string Architecture(HANDLE process, std::string& bits) {
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    auto wow2 = reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    USHORT machine = 0, native = 0;
    if (wow2 && wow2(process, &machine, &native)) {
        if (!machine) machine = native;
        if (machine == IMAGE_FILE_MACHINE_I386) { bits = "32"; return "x86"; }
        if (machine == IMAGE_FILE_MACHINE_AMD64) { bits = "64"; return "x64"; }
        if (machine == IMAGE_FILE_MACHINE_ARM64) { bits = "64"; return "arm64"; }
        return "unknown";
    }
    BOOL wow = FALSE;
    if (!IsWow64Process(process, &wow)) return "unknown";
    SYSTEM_INFO system{}; GetNativeSystemInfo(&system);
    if (wow || system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) { bits = "32"; return "x86"; }
    if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) { bits = "64"; return "x64"; }
    if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) { bits = "64"; return "arm64"; }
    return "unknown";
}
}

static Reply CmdDoctor(DWORD pid, const Args& a) {
    using namespace vi_doctor;
    if (!pid) return Fail("doctor needs --pid N");
    if (!a.pos.empty()) return Fail("doctor takes no positional arguments; use --pid N and optional --key KEY");
    for (const auto& opt : a.opt) if (opt.first != "key") return Fail("doctor: unknown option --" + opt.first);
    int key = 0;
    if (HasOpt(a, "key")) { key = vi_keys::VkFromName(GetOpt(a, "key")); if (!key) return Fail("doctor --key needs one valid key name (not a chord)"); }
    std::vector<std::string> suggestions;
    std::string bits = "null", arch = "unknown", alive = "null";
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    DWORD processError = process ? ERROR_SUCCESS : GetLastError();
    if (process) { arch = Architecture(process, bits); alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT ? "true" : "false"; CloseHandle(process); }
    else if (processError == ERROR_INVALID_PARAMETER) alive = "false";
    const TargetToken token = QueryTargetToken(pid);
    std::string st, queryError;
    bool available = alive != "false" && Query(pid, "{\"cmd\":\"status\"}", st, queryError) && Field(st, "ok") == "true";
    if (!available && queryError.empty()) queryError = alive == "false" ? "process is not running" : "status reply was not successful";
    if (available && bits == "null") { bits = Field(st, "bits"); if (bits == "32") arch = "x86"; else if (bits == "64") arch = "x64"; }
    if (available && alive == "null") alive = "true";
    const std::string appContainer = token.known ? token.appContainer ? "true" : "false" : available ? Field(st, "app_container") : "null";
    if (!available) suggestions.push_back(alive == "false" ? "The process is gone; use the PID returned by a new launch." : "No status is available. Check the PID, process access and injection result; doctor does not inject.");
    std::string counters = available ? Field(st, "counters") : "null";
    const char* channel[][2] = {
        {"GetAsyncKeyState", "gaks"}, {"GetKeyState", "gks"}, {"GetKeyboardState", "gkbs"},
        {"DirectInputKeyboardState", "di_kbd"}, {"DirectInputMouseState", "di_mouse"},
        {"DirectInputKeyboardBuffer", "di_data_kbd"}, {"DirectInputMouseBuffer", "di_data_mouse"},
        {"GetRawInputData", "rid"}, {"GetRawInputBuffer", "rib"}, {"XInput", "xinput"},
        {"WindowKeyboardMessages", "wm_key"}, {"WindowMouseMessages", "wm_mouse"}
    };
    std::string channels = "{", observed = "["; bool any = false;
    for (size_t i = 0; i < sizeof(channel)/sizeof(channel[0]); ++i) {
        std::string calls = Field(counters, channel[i][1]); bool seen = calls != "null" && strtod(calls.c_str(), nullptr) != 0;
        if (i) channels += ',';
        channels += String(channel[i][0]) + ":{\"calls\":" + calls + ",\"observed\":" + (calls == "null" ? "null" : seen ? "true" : "false") + ",\"source\":" + String("status.counters." + std::string(channel[i][1])) + "}";
        if (seen) { if (any) observed += ','; observed += String(channel[i][0]); any = true; }
    }
    channels += '}'; observed += ']';
    if (available && !any) suggestions.push_back("No input API calls or dispatched input messages have been observed yet. Wait for the game (or a mod loader) to finish starting, then compare trace snapshots around an input command.");
    bool foreground = ForegroundIsTarget(GetForegroundWindow(), pid);
    const std::string guard=available?Field(st,"foreground_guard"):"null";
    const std::string humanIntent=available?Field(st,"user_took_foreground"):"null";
    const std::string cursorConfigured=available?Field(st,"cursor_mode_name"):"null";
    const std::string cursorEffective=available?Field(st,"effective_cursor"):"null";
    std::string claim = available ? Field(st, "claim") : "null";
    if (available && Field(claim, "active") == "true") suggestions.push_back("This process is claimed. Use the reported owner for input and release it with unclaim when finished; doctor is read-only and does not renew the claim.");
    if (humanIntent=="true") suggestions.push_back("The foreground guard is honoring human foreground intent. Coordinate with the human; pause automated input while they are playing.");
    else if (foreground) suggestions.push_back("The target currently holds the real foreground. Keep the shared LIVE lock until foreground_restored is verified; inspect the foreground guard and its lease.");
    if(humanIntent=="true" && cursorEffective=="\"virtual\"") suggestions.push_back("Manual mouse control still uses a virtual cursor because that mode was explicitly configured. Use config cursor=auto before manual mouse control; human foreground intent does not erase forced virtual mode.");
    if(available && humanIntent!="true" && Field(guard,"enabled")=="true" && Field(guard,"armed")=="false") suggestions.push_back("The foreground guard has not observed an external restore destination. Inspect launch and foreground state; perform an explicit restore only under the shared LIVE lock.");
    if(available && strtod(Field(guard,"failed_count").c_str(),nullptr)>0) suggestions.push_back("The foreground guard has recorded failed hand-backs. Inspect its latest state and the target log; the counter alone does not identify why Windows refused or delayed restoration.");
    if (available && Field(st, "isolate") == "false") suggestions.push_back("Isolation is disabled: real input may reach this background target. Restore isolate=true when the test no longer needs it.");
    if (available && Field(st, "blockfocus") == "false") suggestions.push_back("Foreground protection is disabled (blockfocus=false). Re-enable blockfocus=true for background QA.");
    std::string frameApi = available ? Field(st, "frame_api") : "null";
    std::string capturePath = available ? Field(st, "capture_path") : "null";
    const std::string captureReported = capturePath;
    const bool unreportedCapturePath = available && capturePath == "\"none\"" && strtod(Field(counters, "captures").c_str(), nullptr) > 0;
    if (unreportedCapturePath) { capturePath = "null"; suggestions.push_back("The target reports capture activity but no path. Failed attempts or an older build that omits PrintWindow can cause this; inspect the capture command result. No successful capture is inferred from this counter."); }
    if (available && capturePath == "\"none\"") suggestions.push_back("No capture has completed yet. capture_path is the last successful path, not a guarantee that native capture is available.");
    if (available && frameApi == "\"none\"") suggestions.push_back("No intercepted game Present has been observed. PrintWindow may work; native capture needs a supported renderer and a presenting game window.");
    if (appContainer == "true") suggestions.push_back("AppContainer keyboard events may bypass window messages. Use pad for games that read XInput; captures are staged in an accessible directory by the CLI.");
    if (available && Field(counters, "wm_input_dropped") != "null" && strtod(Field(counters, "wm_input_dropped").c_str(), nullptr) > 0)
        suggestions.push_back("Background isolation dropped external WM_INPUT. Another in-process tool's own synthetic raw handles can be dropped too; use a controlled isolate=false test only when appropriate.");
    std::string rawRegistration=available?Field(st,"raw_registration"):"null",rawDelivery=available?Field(st,"raw_delivery"):"null";
    if(rawRegistration!="null"&&Field(rawRegistration,"query_ok")=="false")suggestions.push_back("Raw registration query failed; do not infer that the game has no subscription. Inspect raw_registration.query_error.");
    if(available&&Field(rawRegistration,"query_ok")=="true"&&Field(st,"raw_mouse")=="false"&&strtod(Field(counters,"reg_raw_removed").c_str(),nullptr)>0)suggestions.push_back("No mouse raw subscription is currently registered, and a successful removal was observed. Auto respects removal; raw=post is an explicit override for a controlled WM_INPUT reader. The removal's cause is unknown.");
    if(rawDelivery!="null"&&Field(rawDelivery,"buffer_supported")=="false")suggestions.push_back("This x86 build posts synthetic raw records in auto mode; synthetic raw=buffer is unsupported and rejected. Use raw=post to select WM_INPUT explicitly.");
    if(rawDelivery!="null"&&Field(rawDelivery,"last_backend")=="\"buffer\"")suggestions.push_back("Recent successful GetRawInputBuffer activity selected buffered synthetic delivery. Select raw=post for a WM_INPUT-only consumer; counters alone do not prove application consumption.");
    std::string trace = "null", traceError;
    if (available && key) {
        std::string tr;
        if (Query(pid, "{\"cmd\":\"trace\",\"key\":\"vk" + std::to_string(key) + "\"}", tr, traceError) && Field(tr, "ok") == "true") trace = tr;
        else if (traceError.empty()) traceError = "per-key trace unavailable";
    }
    if (available) suggestions.push_back("Counters show API calls and dispatch since injection, not the current input path or successful game actions. Use doctor --key KEY for per-key reads, then wait-text or wait-change to verify the response.");
    std::string log = available ? Field(st, "log_path") : "null";
    if (available && log == "null") suggestions.push_back("This target does not report its log path. Injected per-build DLLs normally log under dist/run/logs; compiled-in copies log beside their module under logs.");
    std::string j = "{\"ok\":" + std::string(available ? "true" : "false") + ",\"command\":\"doctor\",\"pid\":" + std::to_string(pid) +
        ",\"process_alive\":" + alive + ",\"process_query_error\":" + std::to_string(processError) + ",\"arch\":" + String(arch) + ",\"bits\":" + bits +
        ",\"app_container\":" + appContainer + ",\"status_available\":" + (available ? "true" : "false") +
        ",\"input_channels\":" + channels + ",\"observed_channels\":" + observed + ",\"capture_path\":" + capturePath + ",\"capture_path_reported\":" + captureReported + ",\"frame_api\":" + frameApi +
        ",\"foreground\":{\"really_foreground\":" + (foreground ? "true" : "false") + ",\"blockfocus\":" + (available ? Field(st, "blockfocus") : "null") +
        ",\"user_took_foreground\":"+humanIntent+",\"guard\":"+guard+"},\"cursor\":{\"configured\":"+cursorConfigured+",\"configured_mode\":"+(available?Field(st,"cursor_mode"):"null")+",\"effective\":"+cursorEffective+"},\"claim\":" + claim + ",\"owner\":" + Field(claim, "owner") + ",\"log_path\":" + log +
        ",\"raw_registration\":"+rawRegistration+",\"raw_delivery\":"+rawDelivery+",\"trace_key\":" + (key ? std::to_string(key) : "null") + ",\"key_trace\":" + trace + ",\"suggestions\":[";
    for (size_t i = 0; i < suggestions.size(); ++i) j += (i ? "," : "") + String(suggestions[i]);
    j += ']';
    if (!available) j += ",\"error\":" + String(queryError);
    if (unreportedCapturePath) j += ",\"capture_path_note\":\"capture activity was reported without a path; inspect the capture result to determine success\"";
    if (!traceError.empty()) j += ",\"trace_error\":" + String(traceError);
    return {available, j + '}'};
}
