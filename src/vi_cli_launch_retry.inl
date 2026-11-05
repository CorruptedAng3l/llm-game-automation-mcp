// One continuation after a proven pre-loader failure. A name is only a filter:
// retained parent ancestry, creation time and exact native image path authorize it.
static bool LaunchFailedLoaderChild(DWORD failedPid,unsigned long long failedBorn,HANDLE failedHandle,LaunchProcessHandles& handles,vi_launch_follow::Process& selected,HWND& window,std::string& reason){
 selected={};window=nullptr;reason="no_proven_same_image_descendant";
 if(!failedHandle||GetProcessId(failedHandle)!=failedPid||!failedBorn){reason="failed_process_identity_unavailable";return false;}
 std::wstring image(32768,L'\0');DWORD length=(DWORD)image.size();
 if(!QueryFullProcessImageNameW(failedHandle,0,image.data(),&length)||!length||length>=image.size()){reason="failed_process_image_unavailable";return false;}image.resize(length);
 const auto slash=image.find_last_of(L"\\/");const auto name=slash==std::wstring::npos?image:image.substr(slash+1);
 const auto failed=LaunchProcess(failedPid,0,name,failedHandle);if(failed.born!=failedBorn){reason="failed_process_identity_changed";return false;}
 vi_launch_follow::Tree branch(failed);handles.Retain(failed,failedHandle);
 const auto until=GetTickCount64()+CliBudget(2500);DWORD stablePid=0;unsigned long long stableBorn=0;ULONGLONG stableAt=0;
 while(GetTickCount64()<until&&CliBudget(1)){
  auto rows=LaunchSnapshot(&branch,name);
  // The launch-wide handle set can include ancestors/siblings of a previously
  // selected leaf. Refresh only identities already proven in this branch.
  for(const auto& held:handles.held)if(branch.Contains(held.process))branch.Remember(LaunchProcess(held.process.pid,held.process.parent,held.process.name,held.handle));
  branch.Observe(rows);
  for(auto& process:rows){process.window=false;if(process.pid==failedPid||!process.alive||!branch.Contains(process)||!vi_launch_follow::Name(process.name,name))continue;
   handles.Retain(process);const auto handle=handles.Find(process.pid,process.born);if(!LaunchRetainedProcess(process.pid,handle,process.born))continue;
   std::wstring actual(32768,L'\0');DWORD count=(DWORD)actual.size();if(!QueryFullProcessImageNameW(handle,0,actual.data(),&count)||!count||count>=actual.size())continue;actual.resize(count);if(_wcsicmp(actual.c_str(),image.c_str()))continue;
   const auto candidate=VisibleWindowOf(process.pid);if(candidate&&WindowClass(candidate)!="#32770"&&!OwnedPopupWindow(candidate)&&!PendingStartupSplash(candidate,process.pid))process.window=true;
  }
  const auto candidates=branch.Candidates(rows,name);
  if(candidates.size()>1){reason="multiple_proven_same_image_descendants";return false;}
  if(candidates.size()==1){const auto& candidate=candidates[0];
   if(stablePid!=candidate.pid||stableBorn!=candidate.born){stablePid=candidate.pid;stableBorn=candidate.born;stableAt=GetTickCount64();}
   if(GetTickCount64()-stableAt>=250){const auto current=VisibleWindowOf(candidate.pid);DWORD owner=0;
    if(current&&GetWindowThreadProcessId(current,&owner)&&owner==candidate.pid&&WindowClass(current)!="#32770"&&!OwnedPopupWindow(current)&&!PendingStartupSplash(current,candidate.pid)&&LaunchRetainedProcess(candidate.pid,handles.Find(candidate.pid,candidate.born),candidate.born)){selected=candidate;window=current;reason="proven_same_image_descendant";return true;}}
  }else{stablePid=0;stableBorn=0;stableAt=0;}
  const auto now=GetTickCount64();if(now>=until)break;Sleep(CliBudget((DWORD)(std::min)(50ULL,until-now)));
 }
 return false;
}
