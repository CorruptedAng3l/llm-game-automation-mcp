// Temporal orchestration stays outside the target's command mutex.
static bool BurstNumber(const Args& a,const char* key,double fallback,double low,double high,bool whole,double& value) {
    if(!HasOpt(a,key)){value=fallback;return true;}
    std::string text=GetOpt(a,key);char* end=nullptr;value=strtod(text.c_str(),&end);
    return !text.empty()&&end&&!*end&&std::isfinite(value)&&value>=low&&value<=high&&(!whole||value==std::floor(value));
}
static bool BurstQuiet(const Args& a,bool& quiet,std::string& error) {
    quiet=false;bool seen=false;
    for(const auto& option:a.opt)if(option.first=="quiet") {
        if(seen){error="capture-burst quiet may appear only once";return false;}seen=true;
        if(option.second=="true")quiet=true;
        else if(option.second!="false"){error="capture-burst quiet must be true or false";return false;}
    }
    return true;
}
static Reply CmdCaptureBurst(DWORD pid,const Args& a) {
    const ULONGLONG commandStarted=GetTickCount64();
    bool quiet=false;std::string quietError;if(!BurstQuiet(a,quiet,quietError))return Fail(quietError);
    if(a.pos.size()!=1||a.pos[0].empty())return Fail("capture-burst needs exactly one contact-sheet path");
    double countValue=0,intervalValue=0,columnsValue=0,timeoutValue=0,frameTimeoutValue=0,qualityValue=0,scaleValue=1,widthValue=0;
    if(!BurstNumber(a,"frames",8,1,16,true,countValue)||!BurstNumber(a,"interval-ms",250,10,5000,true,intervalValue)||
       !BurstNumber(a,"columns",(std::min)(4.0,countValue),1,countValue,true,columnsValue)||
       !BurstNumber(a,"timeout-ms",60000,1,60000,true,timeoutValue)||!BurstNumber(a,"frame-timeout-ms",3000,1,120000,true,frameTimeoutValue))return Fail("burst bounds: frames1..16, interval10..5000ms, columns1..frames, timeout1..60000ms, frame-timeout1..120000ms");
    if((countValue-1)*intervalValue>30000)return Fail("burst scheduled span must not exceed 30000 ms");
    std::string format=GetOpt(a,"format","png");
    if(format!="png"&&format!="jpeg")return Fail("burst format must be png or jpeg");
    if(HasOpt(a,"quality")&&format!="jpeg")return Fail("quality applies only to JPEG captures");
    if(!BurstNumber(a,"quality",85,1,100,true,qualityValue))return Fail("JPEG quality must be a whole number1..100");
    if(HasOpt(a,"scale")&&HasOpt(a,"max-width"))return Fail("use scale or max-width, not both");
    if(!BurstNumber(a,"scale",1,0.05,1,false,scaleValue)||!BurstNumber(a,"max-width",0,1,8192,true,widthValue))return Fail("capture scale must be0.05..1 or max-width1..8192");
    wchar_t full[32768];DWORD length=GetFullPathNameW(U2W(a.pos[0]).c_str(),32768,full,nullptr);
    if(!length||length>=32768)return Fail("invalid contact-sheet path");
    std::string status;
    if(!Request(pid,"{\"cmd\":\"status\"}",status,(DWORD)(std::min)(5000.0,timeoutValue))||status.find("\"ok\":false")!=std::string::npos){std::string detail=JsonText(status,"error");return Fail(detail.empty()?"burst capability status unavailable (target/transport failure)":detail);}if(JsonNum(status,"capture_output_version")<1)return Fail("target lacks map-preserving burst capture; restart the owned target with the matching updated DLL");
    unsigned count=(unsigned)countValue,columns=(unsigned)columnsValue,rows=(count+columns-1)/columns;
    Args captureArgs=a;
    for(auto& option:captureArgs.opt){if(option.first=="scale")option.second=std::to_string(scaleValue);if(option.first=="max-width")option.second=std::to_string((unsigned)widthValue);}
    if(!HasOpt(a,"scale")&&!HasOpt(a,"max-width"))captureArgs.opt.push_back({"max-width","320"});
    const ULONGLONG started=commandStarted,deadline=started+(ULONGLONG)timeoutValue;
    ULONGLONG next=started;unsigned missed=0;
    vi_vision::Image sheet;std::string frames="[",error;
    auto fail=[&](const std::string& why,unsigned completed){return Fail(why,"\"completed_frames\":"+std::to_string(completed)+",\"capture_space_updated\":false,\"coordinate_space\":\"temporal_contact_sheet\",\"frames\":"+frames+"]");};
    for(unsigned index=0;index<count;++index){
        while(GetTickCount64()<next&&GetTickCount64()<deadline){ULONGLONG now=GetTickCount64();if(now>=next)break;Sleep((DWORD)(std::min)(10ULL,next-now));}
        ULONGLONG before=GetTickCount64();if(before>=deadline)return fail("burst total cooperative deadline reached",index);
        Frame frame;unsigned frameTimeout=(unsigned)(std::min)((ULONGLONG)frameTimeoutValue,deadline-before);
        if(!Grab(pid,captureArgs,frame,error,true,&captureArgs,frameTimeout))return fail("burst frame "+std::to_string(index)+": "+error,index);
        ULONGLONG after=GetTickCount64();
        if(!index){
            std::uint64_t width=(std::uint64_t)frame.img.w*columns,height=(std::uint64_t)frame.img.h*rows;
            if(width>8192||height>8192||width*height>16u*1024u*1024u)return fail("contact sheet exceeds8192 edge/16 million pixel budget",0);
            sheet.w=(int)width;sheet.h=(int)height;sheet.pixels.assign((size_t)(width*height*4),0);
        }else if((std::uint64_t)frame.img.w*columns!=(std::uint64_t)sheet.w||(std::uint64_t)frame.img.h*rows!=(std::uint64_t)sheet.h)return fail("target image geometry changed during burst",index);
        int x=(int)(index%columns)*frame.img.w,y=(int)(index/columns)*frame.img.h;
        for(int row=0;row<frame.img.h;++row)memcpy(sheet.pixels.data()+((size_t)(y+row)*sheet.w+x)*4,frame.img.pixels.data()+(size_t)row*frame.img.w*4,(size_t)frame.img.w*4);
        if(index)frames+=",";
        frames+="{\"index\":"+std::to_string(index)+",\"scheduled_ms\":"+std::to_string(next-started)+",\"actual_start_ms\":"+std::to_string(before)+",\"actual_end_ms\":"+std::to_string(after)+",\"lateness_ms\":"+std::to_string(before>next?before-next:0)+",\"missed_intervals_before\":"+std::to_string(missed)+",\"tile_rect\":["+std::to_string(x)+","+std::to_string(y)+","+std::to_string(frame.img.w)+","+std::to_string(frame.img.h)+"],\"hash\":\""+frame.hash+"\",\"method\":\""+JEsc(frame.method)+"\",\"captured_present\":"+(frame.method=="printwindow"?"null":Num(frame.nativeFrame))+",\"client_origin_x\":"+Num(frame.ox)+",\"client_origin_y\":"+Num(frame.oy)+",\"client_per_pixel_x\":"+Num(frame.sx)+",\"client_per_pixel_y\":"+Num(frame.sy)+"}";
        if(!quiet){fprintf(stderr,"[vinput] burst frame %u/%u captured at %llu ms\n",index+1,count,after-started);fflush(stderr);}
        if(after>=deadline)return fail("burst cooperative deadline reached after capture",index+1);
        next+=(ULONGLONG)intervalValue;missed=0;
        if(next<after){missed=(unsigned)((after-next)/(ULONGLONG)intervalValue+1);next+=(ULONGLONG)missed*(ULONGLONG)intervalValue;}
    }
    if(!vi_image::Save(full,sheet,format,(int)qualityValue,error))return fail(error,count);
    return {true,"{\"ok\":true,\"path\":\""+JEsc(W2U(full))+"\",\"format\":\""+format+"\",\"w\":"+std::to_string(sheet.w)+",\"h\":"+std::to_string(sheet.h)+",\"columns\":"+std::to_string(columns)+",\"rows\":"+std::to_string(rows)+",\"coordinate_space\":\"temporal_contact_sheet\",\"capture_space_updated\":false,\"order\":\"row-major chronological\",\"clock\":\"real GetTickCount64 milliseconds\",\"timeout_cooperative\":true,\"elapsed_ms\":"+std::to_string(GetTickCount64()-started)+",\"frames\":"+frames+"]}"};
}
