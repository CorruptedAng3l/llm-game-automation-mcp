// GUI continuity does not recover input lifetime, application release, or clip authority.
std::string WndBindingStatusJson(HWND window){
 const auto binding=WndBindingFind(window);const bool probed=WndBindingLive(binding);
 unsigned quarantined=0;{std::lock_guard<std::mutex> lock(g_wndBindingMx);for(const auto& item:g_wndBindings)if(item.second->trustLost.load())++quarantined;}
 const unsigned lost=binding?binding->trustLost.load():0,reason=binding?binding->lastLoss.load():0;const bool trusted=probed&&lost==0;JOut out;out.num("version",1);out.bol("current_binding_present",binding!=nullptr);
 out.bol("lifetime_trusted",trusted);out.bol("current_quarantined",lost!=0);out.bol("gui_predecessor_usable",WndBindingGuiUsable(binding));
 out.num("quarantined_bindings",quarantined);out.num("property_missing",(double)g_wndPropertyMissing.load());out.num("property_mismatch",(double)g_wndPropertyMismatch.load());out.num("owner_or_thread_changed",(double)g_wndOwnerChanged.load());
 out.num("gui_forwarded_without_witness",(double)g_wndGuiUnconfirmed.load());out.num("unusable_record_defaulted",(double)g_wndGuiDefaulted.load());out.num("input_refused",(double)g_wndInputRefused.load());out.num("destroy_records_retired",(double)g_wndDestroyRetired.load());
 out.str("last_reason",!binding?"no_binding":reason==4?"owner_or_thread_changed":reason==2?"property_mismatched":reason==1?"property_missing":binding->destroying?"destroying":"none");
 out.bol("same_hwnd_recovery_supported",false);out.bol("automatic_restamp_supported",false);
 out.str("application_release_state",lost?"unknown after lifetime witness loss":"not measured");
 out.str("recovery_action","release local input and drain the old queue, then restart the target or select a fresh safely adoptable window; same-HWND recovery is unsupported");
 out.str("scope","saved immutable A/W predecessor may keep ordinary GUI callbacks alive; lost binding trust remains quarantined even if its old property value is restored; no lifetime continuity or application input-consumption guarantee");return out.done();
}
