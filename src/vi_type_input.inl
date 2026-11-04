// Included after the JSON/key helpers. Receipts count accepted UTF-16 input units,
// not character dispatch or application consumption. Never retry partial input.
std::string TypeInput(const JObj& request){
    const std::wstring text=U2W(JStr(request,"text"));const double delay=JNum(request,"delay",30);
    const bool keys=JBool(request,"keys",false);size_t accepted=0;DWORD error=0;bool cleanupFailed=false,unitPartial=false;
    if(!g_hwnd)return ErrJ("no window");
    if(!std::isfinite(delay)||delay<0||delay>60000)return ErrJ("type delay must be finite0..60000 ms; no input was posted");
    if(keys&&MouseKeyboardIsolating()&&!g_translationNoState)return ErrJ("isolated type --keys translation requires Windows10 1607+; use Unicode type without --keys");
    PostedMsg commandTarget;if(!PostedCaptureTarget(commandTarget))return ErrJ("type target is invalid; no input was posted");
    unsigned long long pressedGeneration[256]{};
    auto key=[&](int vk,bool down,bool suppress=false){
        if(down){const bool ok=KeyEvent(vk,true,suppress,&commandTarget,&pressedGeneration[vk]);if(!ok&&!error)error=GetLastError();return ok;}
        // Own only the generation this synchronous type actually changed. A
        // later command/job must not be erased by delayed cleanup.
        std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);const auto owned=pressedGeneration[vk];pressedGeneration[vk]=0;
        if(!owned)return true;
        const int resource=JobResource(vk);
        if(g_resourceGeneration[resource]!=owned||g_resourceOwner[resource]!=0){if(!error)error=ERROR_RETRY;return false;}
        if(!PostedTargetCurrent(commandTarget)){
            JobsOverrideKey(vk);SetVkState(vk,false);
            const UINT scan=ScanOf(vk);DiPush(true,scan|(IsExtendedVk(vk)?0x80:0),0);
            // Do not post legacy/raw messages into the newly adopted HWND.
            if(!error)error=ERROR_RETRY;return false;
        }
        const bool ok=KeyEvent(vk,false,suppress,&commandTarget);if(!ok&&!error)error=GetLastError();return ok;
    };
    for(wchar_t ch:text){
        if(!PostedTargetCurrent(commandTarget)){error=ERROR_RETRY;break;}
        if(keys){
            const HKL layout=GetKeyboardLayout(commandTarget.thread);
            const SHORT mapping=layout?VkKeyScanExW(ch,layout):(SHORT)-1;
            if(mapping==-1){error=ERROR_NO_UNICODE_TRANSLATION;break;}
            const int vk=mapping&0xff;const int modifiers[]={VK_SHIFT,VK_CONTROL,VK_MENU};const int bits[]={0x100,0x200,0x400};
            std::vector<int> pressed;bool ok=true,keyAttempted=false;
            for(int i=0;i<3;i++)if((mapping&bits[i])&&!g_vk[modifiers[i]]){pressed.push_back(modifiers[i]);unitPartial=true;if(!key(modifiers[i],true)){ok=false;break;}}
            if(ok){keyAttempted=true;unitPartial=true;ok=key(vk,true);}
            if(ok)Sleep(20);
            if(keyAttempted&&!key(vk,false)){ok=false;cleanupFailed=true;}
            for(size_t i=pressed.size();i-- >0;)if(!key(pressed[i],false)){ok=false;cleanupFailed=true;}
            if(!ok)break;
            ++accepted;
        }else if(ch==L'\n'){
            // Preserve Enter key/raw/DI events, but enqueue its CR explicitly in
            // text order. Suppress only these Return publications' automatic
            // translation, including foreground/opt-out, to avoid duplicate CR.
            unitPartial=true;const bool down=key(VK_RETURN,true,true);bool character=false;
            if(down){character=PostTranslated(commandTarget,WM_CHAR,L'\r',1);if(!character&&!error)error=GetLastError();if(character)++accepted;}
            if(down&&character)Sleep(20);
            if(!key(VK_RETURN,false,true))cleanupFailed=true;
            if(!down||!character||cleanupFailed)break;
        }else{
            if(!PostTranslated(commandTarget,WM_CHAR,(WPARAM)ch,1)){error=GetLastError();break;}
            ++accepted;
        }
        SleepMs(delay);
    }
    const bool ok=accepted==text.size()&&!error&&!cleanupFailed;
    JOut out;out.bol("ok",ok);out.num("requested_utf16_units",(double)text.size());out.num("accepted_utf16_units",(double)accepted);
    out.bol("partial",!ok&&(accepted>0||unitPartial));out.num("post_error",error);out.str("posting_stage",keys?"key_messages":"character_messages");
    out.bol("translation_pending",keys);out.bol("delivery_confirmed",false);out.bol("cleanup_message_release_failed",cleanupFailed);
    out.str("receipt_scope",keys?"key publication accepted; translated characters and application consumption remain unconfirmed":"UTF-16 character publications accepted; application consumption remains unconfirmed");
    if(!ok)out.str("error","type stopped at the first unaccepted unit/channel; accepted prefix was not retried; inspect post_error and cleanup_message_release_failed");
    return out.done();
}
