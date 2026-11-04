// Included after JOut. These observations are metadata, not input-coordinate authority.
std::string CaptureDiagHandle(HWND window){char text[32];sprintf_s(text,"0x%llX",(unsigned long long)(ULONG_PTR)window);return text;}
std::string CaptureDiagWindowJson(const CaptureDiagWindow& item){
 if(!item.window)return "null";JOut o;o.str("hwnd",CaptureDiagHandle(item.window));o.num("pid",item.pid);o.num("thread",item.thread);
 o.str("class",W2U(item.klass));o.str("title",W2U(item.title));o.str("title_source","default_window_caption_not_custom_wm_gettext");o.bol("title_available",item.titleAvailable);o.str("title_encoding",item.unicode?"unicode_default_proc":"ansi_default_proc_acp_to_utf16");o.bol("title_may_be_truncated",item.titleLimit);
 o.num("client_w",item.width);o.num("client_h",item.height);o.bol("visible",item.visible);o.bol("iconic",item.iconic);o.str("owner_hwnd",CaptureDiagHandle(item.owner));
 o.str("lifetime_cookie",std::to_string(item.lifetime));o.bol("same_process_at_observation",item.valid);o.bol("lifetime_and_client_size_stable_at_reply",CaptureDiagCurrent(item));
 const char* kind=item.klass.find(L"D3DProxyWindow")!=std::wstring::npos?"d3d_proxy":item.klass.find(L"PresentationWindow")!=std::wstring::npos?"presentation_window":"other_top_level";
 o.str("candidate_kind",kind);return o.done();
}
std::string CaptureDiagnosticsJson(bool printWindow){
 JOut o;o.num("version",1);const auto& d=g_captureDiagnostic;o.bol("available",printWindow&&d.available);
 if(!printWindow||!d.available){o.str("reason","native_capture_source_window_not_observed_by_this_diagnostic");return o.done();}
 o.raw("input",CaptureDiagWindowJson(d.input));o.raw("source",CaptureDiagWindowJson(d.source));o.str("input_adoption_generation",std::to_string(d.adoption));
 o.bol("input_adoption_stable_at_reply",CaptureDiagAdoptionCurrent(d));
 JOut enumeration;enumeration.str("scope","visible_top_level_windows_observed_in_target_process");enumeration.num("visited",d.visited);enumeration.num("visit_limit",4096);enumeration.num("retained_limit",16);
 enumeration.num("best_effort_elapsed_budget_ms",12);enumeration.num("candidates_observed",d.candidatesObserved);enumeration.bol("truncated",d.enumerationTruncated||d.retainedTruncated);enumeration.bol("enumeration_truncated",d.enumerationTruncated);enumeration.bol("retained_truncated",d.retainedTruncated);enumeration.str("stop_reason",d.enumerationStop);
 std::string candidates="[";bool first=true;bool proxy=false;for(const auto& item:d.candidates){if(!first)candidates+=',';first=false;candidates+=CaptureDiagWindowJson(item);if(item.klass.find(L"D3DProxyWindow")!=std::wstring::npos)proxy=true;}candidates+=']';enumeration.raw("candidates",candidates);o.raw("enumeration",enumeration.done());
 JOut observation;observation.str("space","full_printwindow_client_image_before_region_and_output_transforms");observation.str("composition","existing_presentation_crop_if_used_otherwise_input_client");observation.bol("available",d.pixelsObserved);observation.raw("uniform_rgb",d.pixelsObserved?(d.uniform?"true":"false"):"null");observation.raw("all_black_rgb",d.pixelsObserved?(d.black?"true":"false"):"null");observation.str("meaning","pixel_observation_only_not_output_region_black_wrong_source_or_frozen_render_diagnosis");o.raw("observation",observation.done());
 JOut transform;transform.str("space","printwindow_source_to_full_input_client_before_region_and_output_transforms");transform.str("policy",d.transform);transform.num("source_x",d.sourceX);transform.num("source_y",d.sourceY);transform.num("source_w",d.sourceW);transform.num("source_h",d.sourceH);transform.num("output_w",d.outputW);transform.num("output_h",d.outputH);transform.bol("input_target_changed",false);o.raw("transform",transform.done());
 if(d.pixelsObserved&&d.black&&proxy)o.str("note","observed black capture and another same-process D3DProxyWindow; candidate metadata does not prove a usable capture source or input transform");
 o.bol("automatic_proxy_selection",false);o.bol("candidate_pixels_probed",false);return o.done();
}
