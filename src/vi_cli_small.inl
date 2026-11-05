// vi_cli_small.inl - pixel / scroll / drag (included by vi_cli_ui.inl).
// ---- small commands ----------------------------------------------------------------------------------------------
static Reply CmdPixel(DWORD pid, const Args& a) {
    if (a.pos.size() < 2) return Fail("pixel needs X Y (client pixels)");
    const int x = atoi(a.pos[0].c_str()), y = atoi(a.pos[1].c_str());
    Frame f; std::string err; if (!Grab(pid, a, f, err)) return Fail(err);
    const int px = (int)std::floor((x - f.ox) / f.sx), py = (int)std::floor((y - f.oy) / f.sy);
    vi_vision::Pixel p;
    if (!vi_vision::ReadPixel(f.img, px, py, p, err)) return Fail("pixel (" + std::to_string(x) + "," + std::to_string(y) + "): " + err);
    char hex[16]; sprintf_s(hex, "#%02X%02X%02X", p.r, p.g, p.b);
    return {true, "{\"ok\":true,\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"r\":" + std::to_string(p.r) + ",\"g\":" + std::to_string(p.g) + ",\"b\":" + std::to_string(p.b) + ",\"hex\":\"" + hex + "\",\"frame_hash\":\"" + f.hash + "\"}"};
}
static Reply CmdScroll(DWORD pid, const Args& a) {
    if (a.pos.empty() || !IsNumber(a.pos[0])) return Fail("scroll needs N notches (positive = up/away, negative = down) and optional X Y");
    const int notches = atoi(a.pos[0].c_str());
    if (notches == 0 || notches > 1000 || notches < -1000) return Fail("scroll N must be 1..1000 in magnitude");
    std::string resp;
    if (a.pos.size() >= 3) {
        std::string m = "{"; PutS(m, "cmd", "mouse"); Put(m, "x", a.pos[1]); Put(m, "y", a.pos[2]);
        if (HasOpt(a, "norm")) Put(m, "norm", "true");
        m += "}";
        if (!Request(pid, m, resp) || resp.find("\"ok\":true") == std::string::npos) return {false, resp};
    } else if (a.pos.size() == 2) return Fail("scroll needs both X and Y, or neither");
    const double gap = atof(GetOpt(a, "notch-ms", "30").c_str());
    for (int i = 0; i < abs(notches); ++i) {
        std::string w = "{"; PutS(w, "cmd", "wheel"); Put(w, "delta", notches > 0 ? "120" : "-120"); if (HasOpt(a, "horizontal")) Put(w, "horizontal", "true");
        w += "}";
        if (!Request(pid, w, resp) || resp.find("\"ok\":true") == std::string::npos) return {false, resp};
        if (i + 1 < abs(notches)) Sleep((DWORD)gap);
    }
    return {true, "{\"ok\":true,\"notches\":" + std::to_string(notches) + ",\"delta_each\":" + std::string(notches > 0 ? "120" : "-120") + "}"};
}
static Reply CmdPlacement(DWORD pid,const Args& a){
    if(a.pos.size()!=4)return Fail("placement-drag needs x1 y1 x2 y2 (client pixels)");
    std::string status;if(!Request(pid,"{\"cmd\":\"status\"}",status)||status.find("\"placement_drag\":true")==std::string::npos)return Fail("target lacks owned polled placement support; restart the owned target with the matching updated DLL; no input was sent","\"input_injected\":false,\"placement_preflight_refused\":true");
    if(HasOpt(a,"scope-json")){std::string capability;const bool received=Request(pid,"{\"cmd\":\"ping\",\"pointer_scope_support\":true}",capability);vi_ready_json::Reader capabilities{capability,0,{}};bool accepted=false;
        if(!received||!capabilities.Parse()||!capabilities.Bool("ok",accepted)||!accepted||!capabilities.fields.count("placement_scope_version")||capabilities.fields.at("placement_scope_version").type!='n'||capabilities.fields.at("placement_scope_version").raw!="1"||!capabilities.fields.count("pid")||capabilities.fields.at("pid").raw!=std::to_string(pid))return Fail("target lacks scoped placement admission; no input was sent","\"input_injected\":false,\"placement_preflight_refused\":true");}
    std::string command="{";PutS(command,"cmd","placement-drag");Put(command,"x",a.pos[0]);Put(command,"y",a.pos[1]);Put(command,"x2",a.pos[2]);Put(command,"y2",a.pos[3]);
    for(auto& option:a.opt){std::string key=option.first;if(key=="placement")continue;if(key=="capture-space"){PutS(command,"space","capture");continue;}if(key=="scope-json"){PutS(command,"expected_capture_scope",option.second);continue;}if(key=="dwell-ms")key="dwell_ms";if(key=="poll-timeout-ms")key="poll_timeout_ms";if(key=="hold-ms")key="hold_ms";
        if(key!="steps"&&key!="ms"&&key!="dwell_ms"&&key!="poll_timeout_ms"&&key!="hold_ms"&&key!="button"&&key!="norm"&&key!="async"&&key!="replace")return Fail("unsupported placement option: --"+option.first+"; no input was sent");Put(command,key,option.second);}
    command+="}";std::string response;bool received=Request(pid,command,response);return {received&&response.find("\"ok\":true")!=std::string::npos,response};
}
static Reply CmdDrag(DWORD pid, const Args& a) {
    if(HasOpt(a,"placement"))return CmdPlacement(pid,a);
    if (a.pos.size() < 4) return Fail("drag needs x1 y1 x2 y2 (client pixels)");
    const std::string button = GetOpt(a, "button", "left");
    const double ms = atof(GetOpt(a, "ms", "300").c_str());
    if (!(ms >= 0 && ms <= 60000)) return Fail("--ms must be 0..60000");
    const std::string pace = GetOpt(a, "pace", "frames");
    if (pace != "frames" && pace != "time") return Fail("--pace must be frames (default) or time");
    const double frameTimeout = atof(GetOpt(a, "frame-timeout-ms", "1000").c_str());
    if (!(frameTimeout >= 1 && frameTimeout <= 5000)) return Fail("--frame-timeout-ms must be 1..5000");
    int steps = (int)(ms / 20); steps = steps < 3 ? 3 : steps > 200 ? 200 : steps;
    auto coord = [&](std::string& j, const std::string& x, const std::string& y) {
        Put(j, "x", x); Put(j, "y", y); if (HasOpt(a, "norm")) Put(j, "norm", "true"); if (HasOpt(a, "capture-space")) PutS(j, "space", "capture");
    };
    std::string down = "{"; PutS(down, "cmd", "mousedown"); PutS(down, "button", button); coord(down, a.pos[0], a.pos[1]); down += "}";
    std::string move = "{"; PutS(move, "cmd", "mouse"); coord(move, a.pos[2], a.pos[3]); Put(move, "steps", std::to_string(steps)); Put(move, "step_ms", Num(ms / steps));
    Put(move, "frame_paced", pace == "frames" ? "true" : "false"); Put(move, "frame_timeout_ms", Num(frameTimeout)); move += "}";
    // A release must not prepare another hover or wait for rendering. In particular,
    // a stalled frame wait must not leave a held mouse button behind.
    std::string up = "{"; PutS(up, "cmd", "mouseup"); PutS(up, "button", button); up += "}";
    std::string r1, r2, r3;
    bool ok1 = Request(pid, down, r1) && r1.find("\"ok\":true") != std::string::npos;
    if (!ok1) {
        Request(pid, up, r3); // even a lost down reply may have injected the press
        return {false, "{\"ok\":false,\"error\":\"drag: mouse down failed\",\"down\":" + (r1.empty() ? "null" : r1) + "}"};
    }
    bool ok2 = Request(pid, move, r2) && r2.find("\"ok\":true") != std::string::npos;
    bool ok3 = Request(pid, up, r3) && r3.find("\"ok\":true") != std::string::npos;   // always release the button
    const bool ok = ok2 && ok3;
    return {ok, std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"steps\":" + std::to_string(steps) + ",\"ms\":" + Num(ms) + ",\"pacing\":\"" + JsonText(r2, "pacing") + "\",\"frames_per_step\":" + Num(JsonNum(r2, "frames_per_step")) + ",\"down\":" + r1 + ",\"move\":" + (r2.empty() ? "null" : r2) + ",\"up\":" + (r3.empty() ? "null" : r3) + "}"};
}
