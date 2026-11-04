// An older DLL ignores unknown config fields, which could turn a timed opt-out
// into an indefinite one. Probe decoded deadline fields before sending config.
static bool NeedsIsolationDeadline(const std::string& request){
    vi_ready_json::Reader parsed{request,0,{}};if(!parsed.Parse())return true;
    return parsed.fields.count("for_ms")||parsed.fields.count("for")||parsed.fields.count("for-ms")||parsed.fields.count("isolate_for_ms");
}
static Reply RequireIsolationDeadline(DWORD pid){
    std::string response;bool ok=false;
    const bool received=BoundedJobRequest(pid,"{\"cmd\":\"ping\",\"isolation_deadline_support\":true}",1000,response);
    vi_ready_json::Reader parsed{response,0,{}};
    if(received&&parsed.Parse()&&parsed.Bool("ok",ok)&&ok){
        const auto version=parsed.fields.find("isolation_deadline_version");unsigned number=0;
        if(version!=parsed.fields.end()&&version->second.type=='n'&&ExpectUnsigned(version->second.raw,1000,number)&&number>=1)return {true,"{\"ok\":true}"};
    }
    return {false,"{\"ok\":false,\"configuration_changed\":false,\"isolation_deadline_support_checked\":true,\"error\":\"target lacks timed isolation opt-out support or its reply was unavailable; restart the owned target with the matching updated DLL; configuration was not sent\"}"};
}
