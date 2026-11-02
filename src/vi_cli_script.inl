// vi_cli_script.inl - the script runner (included by vi_cli_ui.inl).
// script <file>: one command per line (same syntax as the CLI without --pid), '#' comments, `sleep <ms>`, `echo <text>`.
// Stops at the FIRST failing line and reports it. Every command's reply is printed as one JSON line as it runs.
static Reply RunScript(DWORD pid, const Args& a) {
    if (a.pos.empty()) return Fail("script needs a file");
    FILE* f = nullptr; _wfopen_s(&f, U2W(a.pos[0]).c_str(), L"rb");
    if (!f) return Fail("cannot open script: " + a.pos[0]);
    std::string text; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    const bool keepGoing = HasOpt(a, "continue");
    {   // relative paths inside a script (templates, capture outputs) resolve against the script's own folder
        wchar_t full[MAX_PATH]; if (GetFullPathNameW(U2W(a.pos[0]).c_str(), MAX_PATH, full, nullptr)) {
            wchar_t* slash = wcsrchr(full, L'\\'); if (slash) { *slash = 0; SetCurrentDirectoryW(full); }
        }
    }
    DWORD t0 = GetTickCount(); int lineNo = 0, ran = 0, failed = 0; std::string firstFail; int failLine = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos); if (nl == std::string::npos) nl = text.size();
        std::string l = text.substr(pos, nl - pos); pos = nl + 1; ++lineNo;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
        size_t s = l.find_first_not_of(" \t"); if (s == std::string::npos || l[s] == '#') continue;
        std::vector<std::string> tk = Tokenize(l.substr(s));
        if (tk.empty()) continue;
        Reply r;
        if (tk[0] == "sleep") { Sleep((DWORD)atoi(tk.size() > 1 ? tk[1].c_str() : "100")); r = {true, "{\"ok\":true,\"slept\":true}"}; }
        else if (tk[0] == "echo") { std::string t; for (size_t i = 1; i < tk.size(); ++i) t += (i > 1 ? " " : "") + tk[i]; r = {true, "{\"ok\":true,\"echo\":\"" + JEsc(t) + "\"}"}; }
        else if (tk[0] == "script" || tk[0] == "launch" || tk[0] == "inject" || tk[0] == "list" || tk[0] == "foreground") r = Fail("'" + tk[0] + "' is not allowed inside a script");
        else r = Dispatch(pid, tk);
        ++ran;
        FILE* progress=g_jsonOutput ? stderr : stdout;
        fprintf(progress,"{\"line\":%d,\"cmd\":\"%s\",\"reply\":%s}\n", lineNo, JEsc(tk[0]).c_str(), r.json.c_str()); fflush(progress);
        if (!r.ok) {
            ++failed;
            if (firstFail.empty()) failLine = lineNo, firstFail = "{\"failed_line\":" + std::to_string(lineNo) + ",\"line\":\"" + JEsc(l.substr(s)) + "\",\"reply\":" + r.json + "}";
            if (!keepGoing) break;
        }
    }
    if (failed) return {false, "{\"ok\":false,\"error\":\"script failed at line " + std::to_string(failLine) + "\",\"stopped\":" + (keepGoing ? "false" : "true") + ",\"commands_run\":" + std::to_string(ran) + ",\"failures\":" + std::to_string(failed) + ",\"first_failure\":" + firstFail + ",\"elapsed_ms\":" + std::to_string(GetTickCount() - t0) + "}"};
    return {true, "{\"ok\":true,\"script\":\"" + JEsc(a.pos[0]) + "\",\"commands_run\":" + std::to_string(ran) + ",\"elapsed_ms\":" + std::to_string(GetTickCount() - t0) + "}"};
}
