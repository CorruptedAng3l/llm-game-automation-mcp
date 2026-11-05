std::string RawBufferRoutingJson(const RawReg& registrations){
    JOut out;out.num("version",1);const DWORD keyboard=GetWindowThreadProcessId(registrations.kbdTarget?registrations.kbdTarget:g_hwnd,nullptr),mouse=GetWindowThreadProcessId(registrations.mouseTarget?registrations.mouseTarget:g_hwnd,nullptr);
    out.num("keyboard_thread",keyboard);out.num("mouse_thread",mouse);out.bol("keyboard_recent_buffer_reader",RawBufferRecent(keyboard));out.bol("mouse_recent_buffer_reader",RawBufferRecent(mouse));
    {std::lock_guard<std::mutex> lock(g_rawQMx);out.num("pending_packets",(double)g_rawQ.size());out.num("retained_reader_threads",(double)g_rawBufferReaders.size());}
    out.num("revoked_packets",(double)g_rawBufferRevoked.load());out.num("foreign_thread_packet_checks",(double)g_rawBufferForeignThreadChecks.load());out.str("scope","virtual packets stay on their destination HWND/thread and expire on lifetime/binding change; checks are not gameplay or unique dropped packets");return out.done();
}
