// No cache: class recognition is tied to the currently retained binding and
// generation. Queries are read-only; they confer no new input/owner authority.
FrameworkWindow FrameworkProfileForCurrent() {
    std::lock_guard<std::recursive_mutex> input(g_inputMx);FrameworkWindow out;
    const HWND window=g_hwnd;const auto generation=g_inputReadyGeneration.load();
    const auto binding=WndBindingFind(window);if(!window||!WndBindingLive(binding))return out;
    DWORD owner=0;const DWORD thread=GetWindowThreadProcessId(window,&owner);
    if(!thread||owner!=g_pid||!GetClassNameW(window,out.className,128))return {};
    DWORD afterOwner=0;
    if(g_hwnd!=window||g_inputReadyGeneration!=generation||WndBindingFind(window)!=binding||!WndBindingLive(binding)||
       GetWindowThreadProcessId(window,&afterOwner)!=thread||afterOwner!=g_pid)return {};
    out.window=window;out.generation=generation;out.chromium=vi_framework_input::Chromium(out.className);out.unity=vi_framework_input::Unity(out.className);return out;
}
