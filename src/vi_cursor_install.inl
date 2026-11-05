void* g_cursorOriginals[vi_cursor_guard::ApiCount]={};int g_cursorAlias[vi_cursor_guard::ApiCount]={-1,-1,-1,-1,-1,-1,-1,-1,-1};
template<class Function> bool InstallCursorApi(unsigned index,const char* name,Function detour,Function* original,void* entry){
 g_cursorEntries[index]=entry;
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
 g_cursorInstallFinished=false;
 void* resolved[ApiCount]={};bool keybdConflict[ApiCount]={};
 HMODULE module=GetModuleHandleW(L"user32.dll");
 for(unsigned i=0;i<ApiCount;i++)resolved[i]=module?(void*)GetProcAddress(module,Name(i)):nullptr;
 // Resolve before ANY mutation. A late refusal cannot undo an earlier hook
 // sharing this address with the four-argument keyboard export.
 if(resolved[KeybdEvent])for(unsigned i=0;i<LegacyApiCount;i++)if(resolved[i]==resolved[KeybdEvent]){keybdConflict[i]=true;keybdConflict[KeybdEvent]=true;}
 for(unsigned i=0;i<ApiCount;i++)g_cursorEntries[i]=resolved[i];
 g_cursorInstalled[SetPosition]=!keybdConflict[SetPosition]&&InstallCursorApi(SetPosition,"SetCursorPos",&h_SetCursorPos,&o_SetCursorPos,resolved[SetPosition]);
 g_cursorInstalled[SetPhysicalPosition]=!keybdConflict[SetPhysicalPosition]&&InstallCursorApi(SetPhysicalPosition,"SetPhysicalCursorPos",&h_SetPhysicalCursorPos,&o_SetPhysicalCursorPos,resolved[SetPhysicalPosition]);
 g_cursorInstalled[Clip]=!keybdConflict[Clip]&&InstallCursorApi(Clip,"ClipCursor",&h_ClipCursor,&o_ClipCursor,resolved[Clip]);
 g_cursorInstalled[Send]=!keybdConflict[Send]&&InstallCursorApi(Send,"SendInput",&h_SendInput,&o_SendInput,resolved[Send]);
 g_cursorInstalled[MouseEvent]=!keybdConflict[MouseEvent]&&InstallCursorApi(MouseEvent,"mouse_event",&h_mouse_event,&o_mouse_event,resolved[MouseEvent]);
 g_cursorInstalled[GetClip]=!keybdConflict[GetClip]&&InstallCursorApi(GetClip,"GetClipCursor",&h_GetClipCursor,&o_GetClipCursor,resolved[GetClip]);
 g_cursorInstalled[GetPosition]=!keybdConflict[GetPosition]&&InstallCursorApi(GetPosition,"GetCursorPos",&h_GetCursorPos,&o_GetCursorPos,resolved[GetPosition]);
 g_cursorInstalled[GetPhysicalPosition]=!keybdConflict[GetPhysicalPosition]&&InstallCursorApi(GetPhysicalPosition,"GetPhysicalCursorPos",&h_GetPhysicalCursorPos,&o_GetPhysicalCursorPos,resolved[GetPhysicalPosition]);
 g_cursorInstalled[KeybdEvent]=!keybdConflict[KeybdEvent]&&InstallCursorApi(KeybdEvent,"keybd_event",&h_keybd_event,&o_keybd_event,resolved[KeybdEvent]);
 g_cursorInstallFinished=true;
}
