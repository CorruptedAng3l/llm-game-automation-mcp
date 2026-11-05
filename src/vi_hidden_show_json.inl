void HiddenShowFields(JOut& out){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);HiddenShowExpireLocked();const auto& request=g_hiddenShow;const auto now=HousekeepingNow();JOut state;state.str("ticket",std::to_string(request.ticket));char hwnd[32];sprintf_s(hwnd,"0x%llX",(unsigned long long)(ULONG_PTR)request.scope.window);state.str("hwnd",hwnd);state.str("adoption_generation",std::to_string(request.scope.adoption));state.bol("pending",request.pending);state.bol("completed",request.completed);state.bol("call_attempted",request.callAttempted);state.bol("visible_after",request.visibleAfter);state.str("reason",request.reason);state.num("expires_in_ms",request.pending&&request.expires>now?(double)(request.expires-now):0);state.str("operation","ShowWindow(SW_SHOWNA) on the adopted UI thread; no replay");out.num("show_hidden_version",1);out.raw("hidden_show",state.done());
}
std::string HiddenShowCommand(const JObj& request,bool duplicate){
 if(duplicate)return ErrJ("show-hidden duplicate fields; no visibility request queued");
 const auto found=request.find("hwnd");if(found==request.end()||found->second.t!=1)return ErrJ("show-hidden requires an exact adopted hwnd string");
 const std::string text=found->second.s;if(text.size()<3||text.size()>2+sizeof(ULONG_PTR)*2||text[0]!='0'||(text[1]!='x'&&text[1]!='X'))return ErrJ("show-hidden hwnd must be a bounded hexadecimal handle");
 ULONG_PTR value=0;for(size_t n=2;n<text.size();++n){const auto c=text[n];unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;if(digit>15)return ErrJ("show-hidden hwnd contains a nonhexadecimal character");value=value*16+digit;}
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);HiddenShowExpireLocked();if(g_hiddenShow.pending)return ErrJ("a visibility request is already pending; it is never replayed");
 HiddenShowRequest next;next.scope.window=(HWND)value;if(!value||next.scope.window!=g_hwnd||!g_adoptedAsHidden||!CaptureScopeTake(next.scope))return ErrJ("show-hidden requires the exact currently adopted hidden-origin window and confirmed scope");
 next.binding=WndBindingFind(g_hwnd);next.claimEpoch=g_admittedJobEpoch;next.claimed=g_admittedJobClaimed;next.ownerSeq=g_ownerSeq;next.expires=HousekeepingNow()+5000;
 if(const char* reason=HiddenShowInvalidLocked(next))return ErrJ(std::string("show-hidden refused: ")+reason);
 if(!g_hiddenShowMessage||!next.binding||GetAncestor(g_hwnd,GA_ROOT)!=g_hwnd)return ErrJ("show-hidden UI dispatch is unavailable");
 if(g_hiddenShowSerial==std::numeric_limits<ULONG_PTR>::max())return ErrJ("show-hidden ticket exhausted");next.ticket=++g_hiddenShowSerial;
 next.visibleAfter=IsWindowVisible(g_hwnd)!=FALSE;next.completed=next.visibleAfter;next.pending=!next.visibleAfter;next.reason=next.visibleAfter?"already_visible_no_call":"queued";g_hiddenShow=next;
 if(next.pending&&!PostMessageW(next.scope.window,g_hiddenShowMessage,next.ticket,(LPARAM)kHiddenShowCookie)){g_hiddenShow.pending=false;g_hiddenShow.completed=true;g_hiddenShow.reason="queue_post_failed";JOut out;out.bol("ok",false);HiddenShowFields(out);return out.done();}
 JOut out;out.bol("ok",true);out.bol("visibility_requested",next.pending);out.str("hwnd",text);HiddenShowFields(out);return out.done();
}
