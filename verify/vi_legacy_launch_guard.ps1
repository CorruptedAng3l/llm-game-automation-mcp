. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
# Shared guarded adapter for the original legacy launch-test entrypoints.
. "$PSScriptRoot\vi_retargetrelease_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_launch_lifecycle.ps1"
if(-not('ViWndProcAwTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_launch_tail.cs"}
if(-not('ViLegacyLaunchNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViLegacyLaunchNative{[DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]public static extern IntPtr OpenEventW(uint access,bool inherit,string name);[DllImport("kernel32.dll",SetLastError=true)]public static extern bool SetEvent(IntPtr h);[DllImport("kernel32.dll")]public static extern bool CloseHandle(IntPtr h);}'}
function Assert-ViLegacy([bool]$Good,[string]$Message){if(-not$Good){throw $Message}}
function Read-ViLegacyRecords([string]$Path){if(Test-Path -LiteralPath $Path){foreach($line in ((Get-Content -LiteralPath $Path -Raw)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}}
function Add-ViLegacyActor($Context,$Process,[string]$Image,[long]$Created,[string]$Role){
 # Retain the ORIGINAL returned object before any metadata can throw.
 $a=@{process=$Process;pid=0;path=$Image;created=$Created;role=$Role;retained_handle=[long]0;cache=$null;acked=$false;cleanup=$null};$Context.actors.Add($a)
 $a.pid=$Process.Id;$a.retained_handle=$Process.Handle.ToInt64();if(-not$Created){$a.created=$Process.StartTime.ToUniversalTime().Ticks}
 $a.startup=ObserveStartupImage $Process $Image $a.created $a.retained_handle;Assert-ViLegacy $a.startup.matched 'original live exact kernel PID/birth/nonempty-image startup identity'
 $a.latest=ReadRetainedImage $Process $Image $a.created $a.retained_handle;$a.cache=New-ViStrictCliStartupCache $a.latest $a.retained_handle ([uint32]$a.pid) $a.created $Image;return $a
}
function Complete-ViLegacyActor($A){
 if($A.cleanup){Assert-ViLegacy ($A.cleanup.completion_owned-and-not$A.cleanup.error) 'prior exact cleanup/accounting failed';return};try{$A.cleanup=AccountOrReapCli $A.process $A.path $A.created $A.retained_handle ([uint32]$A.pid) $A.cache}finally{if($A.process){$A.process.Dispose();$A.Remove('process')}}
 Assert-ViLegacy ($A.cleanup.completion_owned-and-not$A.cleanup.error) 'exact retained owned cleanup/readonly exit accounting'
}
function Stop-ViLegacyTargets($H){$problems=[Collections.Generic.List[string]]::new();foreach($c in $H.contexts){foreach($a in $c.actors){try{Complete-ViLegacyActor $a}catch{$problems.Add($_.Exception.Message)}}};if($problems.Count){throw ($problems-join'; ')}}
function Ack-ViLegacyActor($C,$A,$Wait,$Guard){
 Assert-ViLegacy ($Wait.pid-eq$A.pid-and$Wait.role-ceq$A.role-and$Wait.case_nonce-ceq$C.nonce-and[DateTime]::FromFileTimeUtc([long]$Wait.creation_filetime).Ticks-eq$A.created-and$Guard.verified_before_product-eq$true-and$Guard.native_write_trampolines_called-eq$false) 'exact nonce/creation/terminal guard before ACK'
 $name="$($C.prefix)_$($A.role)_$($A.pid)";Assert-ViLegacy ($Wait.ack_name-ceq$name) 'exact private ACK event name'
 $A.latest=ReadRetainedImage $A.process $A.path $A.created $A.retained_handle;Assert-ViLegacy $A.latest.matched 'fresh exact live identity before ACK'
 $event=[ViLegacyLaunchNative]::OpenEventW(2,$false,$name);Assert-ViLegacy ($event-ne[IntPtr]::Zero) 'original owned ACK event exists'
 try{Assert-ViLegacy ([ViLegacyLaunchNative]::SetEvent($event)) 'original owned guard ACK'}finally{[void][ViLegacyLaunchNative]::CloseHandle($event)};$A.acked=$true
}
function Find-ViLegacyActors($C){
 $logs=@(Get-ChildItem -LiteralPath $C.folder -Filter 'cli_*.jsonl');Assert-ViLegacy ($logs.Count-le3) 'bounded expected delegated CLI count'
 foreach($log in $logs){foreach($s in @(Read-ViLegacyRecords $log.FullName|Where-Object event -eq spawned_owned)){
  $old=@($C.actors|Where-Object pid -eq $s.child_pid)|Select-Object -First 1
  if($old){Assert-ViLegacy ([DateTime]::FromFileTimeUtc([long]$s.creation_filetime).Ticks-eq$old.created-and$s.case_nonce-ceq$C.nonce-and$s.role-ceq$old.role) 'repeat creator record matches retained child';continue}
  $parent=@($C.actors|Where-Object pid -eq $s.parent_pid)|Select-Object -First 1;Assert-ViLegacy ($parent-and$parent.acked-and$parent.role-ceq'cli'-and$s.creation_known-eq$true-and$s.case_nonce-ceq$C.nonce-and$s.role-in@('target','cli')) 'creator journal authorizes one initial exact child attachment'
  $image=if($s.role-eq'target'){$C.target}else{$C.child_cli};$p=[Diagnostics.Process]::GetProcessById([int]$s.child_pid);$a=Add-ViLegacyActor $C $p $image ([DateTime]::FromFileTimeUtc([long]$s.creation_filetime).Ticks) ([string]$s.role);$a.creator=$s
 }}
 foreach($log in $logs){$rows=@(Read-ViLegacyRecords $log.FullName);foreach($publication in @($rows|Where-Object event -eq windows_publication_wait)){Assert-ViLegacy ($publication.complete-eq$true) 'actual own window published before production launch enumeration'}
  $wait=@($rows|Where-Object event -eq guard_wait)|Select-Object -First 1;$guard=@($rows|Where-Object {$_.event-eq'terminal_guard'-and$_.stage-eq'before_cli_entry'})|Select-Object -First 1
  if($wait-and$guard){$a=@($C.actors|Where-Object pid -eq $wait.pid)|Select-Object -First 1;if($a-and-not$a.acked){Ack-ViLegacyActor $C $a $wait $guard}}
 }
 $rows=@(Read-ViLegacyRecords $C.target_log);$wait=@($rows|Where-Object event -eq guard_wait)|Select-Object -First 1;$guard=@($rows|Where-Object {$_.event-eq'terminal_guard'-and$_.stage-eq'before_windows'})|Select-Object -First 1
 if($wait-and$guard){$a=@($C.actors|Where-Object role -eq target)|Select-Object -First 1;if($a-and-not$a.acked){Ack-ViLegacyActor $C $a $wait $guard}}
}
function Invoke-ViLegacyBuild([string]$Batch,[string]$Folder){
 $c=@{actors=[Collections.Generic.List[object]]::new()};$p=[Diagnostics.Process]::new();$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$env:ComSpec;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;foreach($v in @('/d','/c',$Batch)){[void]$si.ArgumentList.Add($v)};$si.Environment['TEMP']=$Folder;$si.Environment['TMP']=$Folder;$p.StartInfo=$si
 try{Assert-ViLegacy ($p.Start()) 'owned compiler started';$a=Add-ViLegacyActor $c $p $env:ComSpec 0 compiler;$out=[ViWndProcAwTail]::new($p.StandardOutput);$err=[ViWndProcAwTail]::new($p.StandardError);Assert-ViLegacy ($p.WaitForExit(120000)) 'bounded owned compiler exit'
  $proof=Get-ViExitedCliAccounting $a.retained_handle ([uint32]$a.pid) $a.created $a.path $a.cache;Assert-ViLegacy (Test-ViExitedCliAccounting $proof) 'readonly compiler native exit accounting';$null=$out.Wait(500);$null=$err.Wait(500);Assert-ViLegacy ($out.Complete-and$err.Complete-and-not$out.Error-and-not$err.Error-and$out.Characters-le65536-and$err.Characters-le65536) 'bounded separate complete compiler streams';$text=$out.Text()+$err.Text();[IO.File]::WriteAllText((Join-Path $Folder 'build.log'),$text);Assert-ViLegacy ($proof.native_exit_code-eq0-and$text-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'both guarded wrappers compile with zero warnings';return @{exit=$proof.native_exit_code;exit_accounting=$proof;log=(Join-Path $Folder 'build.log')}
 }finally{foreach($a in $c.actors){Complete-ViLegacyActor $a}}
}
function New-ViLegacyHarness([string]$Root,[string]$SourceRoot,[string]$DllDir,[string]$Out){
 $Root=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Root).Path);$SourceRoot=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $SourceRoot).Path);$DllDir=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $DllDir).Path);$absolute=[IO.Path]::GetFullPath($Out)
 Assert-ViLegacy ($absolute.StartsWith($Root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)-and$absolute.StartsWith('E:\',[StringComparison]::OrdinalIgnoreCase)) 'guard artifacts stay inside explicit E: repository'
 $folder=Join-Path $absolute ('guarded_'+[guid]::NewGuid().ToString('N'));[void](New-Item -ItemType Directory -Force $folder,"$folder\temp");$h=@{root=$Root;source=$SourceRoot;folder=$folder;tools="$folder\tools";contexts=[Collections.Generic.List[object]]::new();builds=[Collections.Generic.List[object]]::new();pins=[Collections.Generic.List[object]]::new()};$mh="$root\third_party\minhook"
 $files=@("$Root\verify\run_launchtest.ps1","$Root\verify\run_foregroundtest.ps1","$Root\verify\test_legacylaunch_preservation.py","$Root\verify\vi_legacy_launch_guard.ps1","$Root\test\vi_legacy_launch_cli.cpp","$Root\test\vi_legacy_launch_target.cpp","$Root\test\vi_launch_terminal.h","$Root\test\vi_launch_ack.h","$Root\test\vi_launch_spawn.h","$Root\test\vi_cursor_spies.h","$Root\src\vi_mh_rename.h","$Root\verify\vi_retargetrelease_identity.ps1","$Root\verify\vi_exited_cli_accounting.ps1","$Root\verify\vi_launch_lifecycle.ps1","$Root\test\vi_launch_tail.cs")+@(Get-ChildItem -LiteralPath "$SourceRoot\src" -File|ForEach-Object FullName)+@(Get-ChildItem -LiteralPath "$mh\src","$mh\src\hde","$mh\include" -File|ForEach-Object FullName)
 foreach($file in $files|Sort-Object -Unique){$h.pins.Add(@{path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash})}
 foreach($arch in @('x64','x86')){
  $where="$folder\tools\$arch";[void](New-Item -ItemType Directory -Force $where,"$where\temp");$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$commands=[Collections.Generic.List[string]]::new();$commands.Add('@echo off');$commands.Add('call "'+$vcDir+'\'+$vc+'" >nul || exit /b 2');$objects=[Collections.Generic.List[string]]::new()
  foreach($unit in @('buffer','hook','trampoline','hde')){$file=if($unit-eq'hde'){Join-Path $mh $(if($arch-eq'x64'){'src\hde\hde64.c'}else{'src\hde\hde32.c'})}else{Join-Path $mh ('src\'+$unit+'.c')};$obj="$where\vimh_$unit.obj";$objects.Add('"'+$obj+'"');$commands.Add('cl /nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /FI"'+$Root+'\src\vi_mh_rename.h" /c "'+$file+'" /Fo:"'+$obj+'" || exit /b 2')}
  $cli=if($arch-eq'x64'){'vinput.exe'}else{'vinput32.exe'};$target=if($arch-eq'x64'){'vi_testinput.exe'}else{'vi_testinput32.exe'};$base='cl /nologo /O2 /MT /EHsc /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"'+$SourceRoot+'\src" /I"'+$Root+'\src" /I"'+$mh+'\include" '
  $commands.Add($base+'/std:c++20 "'+$Root+'\test\vi_legacy_launch_cli.cpp" '+($objects-join' ')+' /Fo:"'+$where+'\cli.obj" /Fe:"'+$where+'\'+$cli+'" /link /SUBSYSTEM:CONSOLE || exit /b 2')
  $commands.Add($base+'/std:c++17 "'+$Root+'\test\vi_legacy_launch_target.cpp" '+($objects-join' ')+' /Fo:"'+$where+'\target.obj" /Fe:"'+$where+'\'+$target+'" /link /SUBSYSTEM:WINDOWS || exit /b 2');$commands.Add('exit /b 0');[IO.File]::WriteAllLines("$where\build.cmd",$commands,[Text.Encoding]::ASCII);$h.builds.Add((Invoke-ViLegacyBuild "$where\build.cmd" "$where\temp"))
  $dll=if($arch-eq'x64'){'vinput64.dll'}else{'vinput32.dll'};Copy-Item -LiteralPath "$DllDir\$dll" -Destination "$where\$dll";foreach($file in @("$where\$cli","$where\$target","$where\$dll")){$h.pins.Add(@{path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash})}
 }
 foreach($name in @('vinput32.exe','vi_testinput32.exe','vinput32.dll')){Copy-Item -LiteralPath "$folder\tools\x86\$name" -Destination "$folder\tools\x64\$name";$expected=(Get-FileHash -LiteralPath "$folder\tools\x86\$name").Hash;Assert-ViLegacy ((Get-FileHash -LiteralPath "$folder\tools\x64\$name").Hash-eq$expected) 'delegated tool/target/DLL copy identity';$h.pins.Add(@{path="$folder\tools\x64\$name";sha256=$expected})}
 foreach($pin in $h.pins){Assert-ViLegacy ((Get-FileHash -LiteralPath $pin.path).Hash-eq$pin.sha256) 'pinned source/executable remains unchanged through build'};return $h
}
function Invoke-ViLegacyCli($H,[string]$Name,[string[]]$Arguments,[string]$ExpectedTarget=''){
 $nonce=[guid]::NewGuid().ToString('N');$folder=Join-Path $H.folder ('case_'+$Name+'_'+$nonce);[void](New-Item -ItemType Directory -Force $folder);$target=$ExpectedTarget;if(-not$target){$target="$($H.tools)\x64\vi_testinput.exe"}
 Assert-ViLegacy ($target-in@("$($H.tools)\x64\vi_testinput.exe","$($H.tools)\x64\vi_testinput32.exe")) 'only exact built guarded target images are spawnable'
 $targetLog="$folder\target.jsonl";for($i=0;$i-lt$Arguments.Count-1;$i++){if($Arguments[$i]-eq'--args'){$m=[regex]::Match($Arguments[$i+1],'^"([^"]+)" -28000 600 (420|640) (260|360)$');Assert-ViLegacy $m.Success 'exact legacy target argument shape';$targetLog=[IO.Path]::GetFullPath($m.Groups[1].Value);Assert-ViLegacy ($targetLog.StartsWith($H.folder.Substring(0,$H.folder.LastIndexOf('\'))+'\',[StringComparison]::OrdinalIgnoreCase)) 'target log remains inside explicit output'}}
 $dll=if($target.EndsWith('32.exe',[StringComparison]::OrdinalIgnoreCase)){"$($H.tools)\x64\vinput32.dll"}else{"$($H.tools)\x64\vinput64.dll"};for($i=0;$i-lt$Arguments.Count-1;$i++){if($Arguments[$i]-eq'--dll'){$dll=$Arguments[$i+1]}}
 $c=@{name=$Name;folder=$folder;nonce=$nonce;prefix="Local\vi_legacy_$nonce";target=$target;target_log=$targetLog;child_cli="$($H.tools)\x64\vinput32.exe";actors=[Collections.Generic.List[object]]::new()};$H.contexts.Add($c)
 $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName="$($H.tools)\x64\vinput.exe";$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$folder
 foreach($entry in @{VI_LAUNCH_GUARD_DIR=$folder;VI_LAUNCH_ACK_PREFIX=$c.prefix;VI_LAUNCH_CASE_NONCE=$nonce;VI_LAUNCH_TARGET=$target;VI_LAUNCH_TARGET_LOG=$targetLog;VI_LEGACY_DLL=$dll;VI_LEGACY_ARGC=[string]$Arguments.Count;VI_LEGACY_ROOT_PID='';TEMP="$($H.folder)\temp";TMP="$($H.folder)\temp"}.GetEnumerator()){$si.Environment[$entry.Key]=[string]$entry.Value}
 for($i=0;$i-lt$Arguments.Count;$i++){[void]$si.ArgumentList.Add($Arguments[$i]);$si.Environment['VI_LEGACY_ARG_'+$i]=$Arguments[$i]}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;$out=$null;$err=$null
 try{Assert-ViLegacy ($p.Start()) 'owned guarded CLI started';$a=Add-ViLegacyActor $c $p $si.FileName 0 cli;$out=[ViWndProcAwTail]::new($p.StandardOutput);$err=[ViWndProcAwTail]::new($p.StandardError);$until=[Environment]::TickCount64+45000
  do{Find-ViLegacyActors $c;if($p.HasExited){break};Start-Sleep -Milliseconds 5}while([Environment]::TickCount64-lt$until)
  Assert-ViLegacy $p.HasExited 'bounded owned CLI deadline';Find-ViLegacyActors $c;$proof=Get-ViExitedCliAccounting $a.retained_handle ([uint32]$a.pid) $a.created $a.path $a.cache;Assert-ViLegacy (Test-ViExitedCliAccounting $proof) 'readonly native main CLI exit accounting'
  $null=$out.Wait(500);$null=$err.Wait(500);Assert-ViLegacy ($out.Complete-and$err.Complete-and-not$out.Error-and-not$err.Error-and$out.Characters-le65536-and$err.Characters-le65536) 'bounded complete separate CLI streams';$stdout=$out.Text();$stderr=$err.Text();[IO.File]::WriteAllText("$folder\stdout.txt",$stdout);[IO.File]::WriteAllText("$folder\stderr.txt",$stderr)
  $lines=@($stdout-split'\r?\n'|Where-Object {$_-match'\S'});$json=[Collections.Generic.List[object]]::new();foreach($line in $lines){try{$json.Add(($line|ConvertFrom-Json))}catch{$json.Add($null)}}
  foreach($row in $json){if($row-and$row.pid){$matched=@($c.actors|Where-Object {$_.role-eq'target'-and$_.pid-eq$row.pid})|Select-Object -First 1;Assert-ViLegacy ($null-ne$matched) 'returned target PID matches creator journal and retained birth/image';$matched.latest=ReadRetainedImage $matched.process $matched.path $matched.created $matched.retained_handle;Assert-ViLegacy $matched.latest.primary_identity_match 'returned target identity has no Path fallback'}}
  foreach($actor in $c.actors){Assert-ViLegacy $actor.acked 'every original CLI/target passed exact guard ACK';if($actor.role-eq'cli'){Complete-ViLegacyActor $actor}}
  return [pscustomobject]@{exit_code=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0);stdout=$stdout;stderr=$stderr;lines=$lines;json=$json.ToArray();targets=@($c.actors|Where-Object role -eq target|ForEach-Object process)}
 }catch{foreach($actor in $c.actors){try{Complete-ViLegacyActor $actor}catch{Write-Warning $_.Exception.Message}};throw}
}
function Save-ViLegacyEvidence($H){
 $contexts=@();foreach($c in $H.contexts){$actors=@();foreach($a in $c.actors){$copy=@{};foreach($key in $a.Keys){if($key-ne'process'){$copy[$key]=$a[$key]}};$actors+=,$copy};$contexts+=@{name=$c.name;folder=$c.folder;nonce=$c.nonce;actors=$actors}}
 @{schema=1;source_root=$H.source;builds=$H.builds;pins=$H.pins;contexts=$contexts;terminal_writers=$true;target_has_gpu_or_di_acquire=$false;original_case_assertions_preserved=$true}|ConvertTo-Json -Depth 35|Set-Content -LiteralPath "$($H.folder)\guard_evidence.json" -Encoding utf8NoBOM
}
