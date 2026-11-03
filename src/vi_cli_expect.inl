// Optional observed postcondition for one original key/hold request. Input is never retried.
static bool HasExpectationOptions(const Args& a) {
    for (const auto& o : a.opt) if (o.first.compare(0, 7, "expect-") == 0) return true;
    return false;
}
static bool ExpectUnsigned(const std::string& s, unsigned limit, unsigned& value) {
    if (s.empty()) return false;
    unsigned n = 0;
    for (unsigned char c : s) {
        if (c < '0' || c > '9' || n > (limit - (unsigned)(c - '0')) / 10) return false;
        n = n * 10 + c - '0';
    }
    value = n; return n <= limit;
}
static Reply ExpectRejected(const std::string& error) {
    return Fail(error, "\"input_injected\":false,\"input_acknowledged\":false,\"input_response_received\":false,\"delivery_unknown\":false,\"expectation_met\":false");
}
// StatusJson can block in the game's UI thread (e.g. GetWindowText). Do not call the
// ordinary synchronous pipe reader for observation/preflight: only read bytes that
// PeekNamedPipe already reports available, under this operation's own deadline.
static bool ExpectStatus(DWORD pid, DWORD budget, std::string& reply, std::string& error, bool& timedOut) {
    timedOut = false;
    const ULONGLONG deadline = GetTickCount64() + budget;
    struct Pipe { HANDLE h = INVALID_HANDLE_VALUE; ~Pipe() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); } } pipe;
    const std::wstring name = PipeName(pid);
    while (GetTickCount64() < deadline) {
        pipe.h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe.h != INVALID_HANDLE_VALUE) break;
        Sleep(1);
    }
    if (pipe.h == INVALID_HANDLE_VALUE) { timedOut = true; error = "status connection deadline expired"; return false; }
    const std::string request = StampOwner("{\"cmd\":\"status\"}") + "\n";
    DWORD written = 0;
    if (!WriteFile(pipe.h, request.data(), (DWORD)request.size(), &written, nullptr) || written != request.size()) { error = "status request write failed"; return false; }
    reply.clear();
    while (GetTickCount64() < deadline) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe.h, nullptr, 0, nullptr, &available, nullptr)) { error = "status pipe closed before a reply"; return false; }
        if (!available) { Sleep(1); continue; }
        char buffer[4096]; DWORD read = 0;
        if (!ReadFile(pipe.h, buffer, (std::min)(available, (DWORD)sizeof(buffer)), &read, nullptr) || !read) { error = "status pipe read failed"; return false; }
        reply.append(buffer, read);
        size_t newline = reply.find('\n');
        if (newline != std::string::npos) {
            reply.resize(newline);
            if (reply.find("\"ok\":true") != std::string::npos) return true;
            error = "status was refused: " + reply; return false;
        }
        if (reply.size() > 65536) { error = "status reply exceeded 64 KiB"; return false; }
    }
    timedOut = true; error = "status reply deadline expired"; return false;
}
static Reply ConfirmInput(DWORD pid, const std::vector<std::string>& tk) {
    if (tk[0] != "key" && tk[0] != "hold") return ExpectRejected("expectation flags apply only to key and hold; no input was injected");
    Args expectationArgs=SplitArgs(std::vector<std::string>(tk.begin()+1,tk.end()));
    if(HasOpt(expectationArgs,"async")&&GetOpt(expectationArgs,"async")!="false")return ExpectRejected("--expect-text requires completed input; --async was rejected before injection");
    std::vector<std::string> input{tk[0]};
    std::map<std::string, std::string> opts;
    for (size_t i = 1; i < tk.size(); ++i) {
        if (tk[i].compare(0, 9, "--expect-") != 0) { input.push_back(tk[i]); continue; }
        std::string option = tk[i].substr(2), value; size_t eq = option.find('=');
        if (eq != std::string::npos) { value = option.substr(eq + 1); option.resize(eq); }
        else {
            if (i + 1 >= tk.size() || tk[i + 1].compare(0, 2, "--") == 0) return ExpectRejected("--" + option + " needs a value; no input was injected");
            value = tk[++i];
        }
        if (option != "expect-text" && option != "expect-timeout-ms" && option != "expect-region") return ExpectRejected("unknown expectation option --" + option + "; no input was injected");
        if (!opts.emplace(option, value).second) return ExpectRejected("duplicate --" + option + "; no input was injected");
    }
    auto text = opts.find("expect-text");
    if (text == opts.end() || text->second.empty() || text->second.size() > 512 || text->second.find_first_not_of(" \r\n\t") == std::string::npos)
        return ExpectRejected("--expect-text needs 1..512 bytes of text; no input was injected");
    unsigned timeout = 3000;
    auto duration = opts.find("expect-timeout-ms");
    if (duration != opts.end() && (!ExpectUnsigned(duration->second, 30000, timeout) || timeout == 0))
        return ExpectRejected("--expect-timeout-ms must be an integer 1..30000; no input was injected");
    unsigned rect[4] = {};
    auto region = opts.find("expect-region");
    if (region != opts.end()) {
        size_t start = 0;
        for (int n = 0; n < 4; ++n) {
            size_t comma = region->second.find(',', start);
            if ((n < 3 && comma == std::string::npos) || (n == 3 && comma != std::string::npos) ||
                !ExpectUnsigned(region->second.substr(start, comma - start), 1000000, rect[n]))
                return ExpectRejected("--expect-region must be x,y,w,h with nonnegative coordinates and positive size; no input was injected");
            start = comma == std::string::npos ? region->second.size() : comma + 1;
        }
        if (!rect[2] || !rect[3]) return ExpectRejected("--expect-region needs positive width and height; no input was injected");
    }
    std::string command, error, original, status;
    bool statusTimedOut = false;
    Args inputArgs = SplitArgs(std::vector<std::string>(input.begin() + 1, input.end()));
    if (HasOpt(inputArgs, "count") && GetOpt(inputArgs, "count") != "1") return ExpectRejected("expectation checks one original key/hold; --count must be 1 or omitted; no input was injected");
    if (!BuildJson(input, command, error)) return ExpectRejected(error);
    if (!ExpectStatus(pid, 1000, status, error, statusTimedOut)) return ExpectRejected("cannot inspect target before input: " + error);
    double width = JsonNum(status, "client_w"), height = JsonNum(status, "client_h");
    if (width <= 0 || height <= 0) return ExpectRejected("expectation needs a target window; no input was injected");
    if (region != opts.end() && ((double)rect[0] + rect[2] > width || (double)rect[1] + rect[3] > height))
        return ExpectRejected("--expect-region is outside the target client area; no input was injected");
    bool responseReceived = Request(pid, command, original);
    bool injected = responseReceived && original.find("\"ok\":true") != std::string::npos;
    // A lost reply or server-side error can follow an applied key. Conservatively
    // retain unknown delivery rather than inviting a duplicate press with false.
    if (!injected) return {false, std::string("{\"ok\":false,\"input_injected\":null,\"delivery_unknown\":true,\"input_acknowledged\":false,\"input_response_received\":") +
        (responseReceived ? "true" : "false") + ",\"expectation_met\":false,\"error\":\"input delivery is unknown after an unacknowledged or failed command; input was not retried\",\"input\":" + (original.empty() ? "null" : original) + "}"};
    DWORD start = GetTickCount(); double before = 0, after = 0; unsigned settled = 0;
    auto remaining = [&]() -> DWORD { DWORD spent = GetTickCount() - start; return spent < timeout ? timeout - spent : 0; };
    Reply observed = Fail("expectation deadline reached before the first fresh OCR frame", "\"timed_out\":true,\"polls\":0,\"ocr_saw\":\"\"");
    if (!ExpectStatus(pid, remaining(), status, error, statusTimedOut))
        observed = Fail("cannot observe post-input frame progress: " + error, std::string("\"timed_out\":") + (statusTimedOut ? "true" : "false") + ",\"polls\":0,\"ocr_saw\":\"\"");
    else {
        bool progressOk = true;
        before = after = JsonNum(status, "frames");
        if (before > 0) {
            while (after - before < 2 && GetTickCount() - start < timeout) {
                Sleep(2);
                if (!ExpectStatus(pid, remaining(), status, error, statusTimedOut)) {
                    progressOk = false;
                    observed = Fail("cannot observe post-input frame progress: " + error, std::string("\"timed_out\":") + (statusTimedOut ? "true" : "false") + ",\"polls\":0,\"ocr_saw\":\"\""); break;
                }
                after = JsonNum(status, "frames");
            }
        } else {
            settled = (std::min)(100u, timeout); Sleep(settled);
        }
        DWORD spent = GetTickCount() - start;
        if (progressOk && (before <= 0 || after - before >= 2) && spent < timeout) {
            Args query; query.pos.push_back(text->second);
            query.opt.push_back({"timeout", std::to_string(timeout - spent)});
            if (region != opts.end()) query.opt.push_back({"region", region->second});
            observed = CmdText(pid, query, 1);
        }
    }
    bool met = observed.ok;
    std::string result = std::string("{\"ok\":") + (met ? "true" : "false") + ",\"input_injected\":true,\"input_acknowledged\":true,\"input_response_received\":true,\"delivery_unknown\":false,\"expectation_met\":" + (met ? "true" : "false") +
        ",\"expected_text\":\"" + JEsc(text->second) + "\",\"expectation_elapsed_ms\":" + std::to_string(GetTickCount() - start) +
        ",\"native_present_before\":" + Num(before) + ",\"native_present_after\":" + Num(after) + ",\"ui_settle_ms\":" + std::to_string(settled) +
        ",\"input\":" + original + ",\"expectation\":" + observed.json +
        ",\"evidence\":\"expected text is observed state; input/API counts do not prove application consumption or causation\"";
    if (!met) result += ",\"error\":\"input was injected but the expected text was not observed; input was not retried\"";
    return {met, result + "}"};
}
