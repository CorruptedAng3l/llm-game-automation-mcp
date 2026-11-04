void* g_cursorOriginals[vi_cursor_guard::ApiCount]={};int g_cursorAlias[vi_cursor_guard::ApiCount]={-1,-1,-1,-1,-1,-1,-1,-1};
template<class Function> bool InstallCursorApi(unsigned index,const char* name,Function detour,Function* original){
 HMODULE module=GetModuleHandleW(L"user32.dll");void* entry=module?(void*)GetProcAddress(module,name):nullptr;g_cursorEntries[index]=entry;
 if(!entry)return false;
 for(unsigned earlier=0;earlier<index;earlier++)if(g_cursorEntries[earlier]==entry){
  const bool compatible=(index==vi_cursor_guard::SetPhysicalPosition&&earlier==vi_cursor_guard::SetPosition)||(index==vi_cursor_guard::GetPhysicalPosition&&earlier==vi_cursor_guard::GetPosition);
  if(!compatible||!g_cursorInstalled[earlier])return false;
  *original=(Function)g_cursorOriginals[earlier];g_cursorOriginals[index]=g_cursorOriginals[earlier];g_cursorDetours[index]=g_cursorDetours[earlier];g_cursorAlias[index]=(int)earlier;return true;
 }
 g_cursorDetours[index]=(void*)detour;const bool installed=HookAddr(name,entry,(void*)detour,(void**)original);g_cursorOriginals[index]=(void*)*original;
 return installed&&*original&&CursorEntryReaches(entry,(void*)detour);
}
void InstallCursorHooks(){
 using namespace vi_cursor_guard;
 g_cursorInstalled[SetPosition]=InstallCursorApi(SetPosition,"SetCursorPos",&h_SetCursorPos,&o_SetCursorPos);
 g_cursorInstalled[SetPhysicalPosition]=InstallCursorApi(SetPhysicalPosition,"SetPhysicalCursorPos",&h_SetPhysicalCursorPos,&o_SetPhysicalCursorPos);
 g_cursorInstalled[Clip]=InstallCursorApi(Clip,"ClipCursor",&h_ClipCursor,&o_ClipCursor);
 g_cursorInstalled[Send]=InstallCursorApi(Send,"SendInput",&h_SendInput,&o_SendInput);
 g_cursorInstalled[MouseEvent]=InstallCursorApi(MouseEvent,"mouse_event",&h_mouse_event,&o_mouse_event);
 g_cursorInstalled[GetClip]=InstallCursorApi(GetClip,"GetClipCursor",&h_GetClipCursor,&o_GetClipCursor);
 g_cursorInstalled[GetPosition]=InstallCursorApi(GetPosition,"GetCursorPos",&h_GetCursorPos,&o_GetCursorPos);
 g_cursorInstalled[GetPhysicalPosition]=InstallCursorApi(GetPhysicalPosition,"GetPhysicalCursorPos",&h_GetPhysicalCursorPos,&o_GetPhysicalCursorPos);
 g_cursorInstallFinished=true;
}
