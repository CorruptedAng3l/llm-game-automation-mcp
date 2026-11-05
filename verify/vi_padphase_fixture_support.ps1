. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
# Same frozen retained-handle authority used by the session-8 native fixtures.
. "$PSScriptRoot\vi_r1_identity.ps1"
. "$PSScriptRoot\vi_exited_cli_accounting.ps1"
. "$PSScriptRoot\vi_r1_lifecycle.ps1"
if(-not('ViPadPhaseTail'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_padphase_tail.cs"}
if(-not('ViR2Pipe'-as[type])){Add-Type -Path "$PSScriptRoot\..\test\vi_r2_pipe.cs"}
$processes=[Collections.Generic.List[object]]::new();$ownedCalls=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new();$instrument=$true
function PhaseRequire([bool]$Good,[string]$Name){if(-not$Good){throw $Name}}
function PhasePin([string]$Path){$pins.Add(@{path=$Path;sha256=(Get-FileHash -LiteralPath $Path).Hash})}
function PhaseLive($T){$T.latest=ReadRetainedImage $T.process $T.path $T.created $T.retained;PhaseRequire $T.latest.matched 'fresh retained kernel PID/creation/exact image before action'}
function PhaseStatus($Target){
 $t=$Target.identity;PhaseLive $t;$wire=[ViR2Pipe]::Query([uint32]$t.pid,$t.retained,$t.created,$t.path,'{"cmd":"status"}',500)
 PhaseRequire ($wire.Complete-and$wire.ServerPid-eq$t.pid-and-not$wire.Error) 'bounded exact-server native status observation';$reply=$wire.Text|ConvertFrom-Json;PhaseRequire ($reply.ok-is[bool]-and$reply.ok) 'typed native status observation'
 $record=@{source='exact_owned_native_readonly_pipe';server_pid=$wire.ServerPid;text=$wire.Text;reply=$reply};$Target.evidence.Add(@{name='bounded native observer transport';record=$record});return $record
}
function PhaseTrack($P,[string]$Image,[bool]$Controller=$false){
 $t=@{process=$P;pid=0;path=$Image;created=[long]0;retained=[long]0;cache=$null;cleanup=$null;controller=$Controller};if(-not$Controller){$processes.Add($t)}
 try{$t.pid=$P.Id;$t.retained=$P.Handle.ToInt64();$t.created=$P.StartTime.ToUniversalTime().Ticks;$t.startup=ObserveStartupImage $P $Image $t.created $t.retained;$t.cache=New-ViStrictCliStartupCache (ReadRetainedImage $P $Image $t.created $t.retained) $t.retained ([uint32]$t.pid) $t.created $Image}catch{$t.startup_error=$_.Exception.Message};return $t
}
function PhaseValidate($T){PhaseRequire (-not$T.startup_error-and$T.startup.matched) 'original returned process has strict live startup identity'}
function PhaseFinish($T){
 if(-not$T-or$T.controller-or$T.cleanup){return}
 try{$T.cleanup=AccountOrReapCli $T.process $T.path $T.created $T.retained ([uint32]$T.pid) $T.cache}catch{$T.cleanup=@{completion_owned=$false;error=$_.Exception.Message}}
 if(-not$T.cleanup.completion_owned-or$T.cleanup.error){$script:instrument=$false;Write-Warning ('owned cleanup/accounting failed: '+$T.cleanup.error)}
}
function PhaseRun([string]$Image,[string[]]$Arguments,[int]$Budget=10000,[bool]$Guarded=$false){
 $Image=(Resolve-Path -LiteralPath $Image).Path;$p=[Diagnostics.Process]::new();$si=[Diagnostics.ProcessStartInfo]::new();$p.StartInfo=$si;$si.FileName=$Image;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$tools
 $si.Environment['TEMP']="$tools\temp";$si.Environment['TMP']="$tools\temp";foreach($arg in $Arguments){[void]$si.ArgumentList.Add($arg)}
 $gate=$null;if($Guarded){$target=if($Arguments[2]-eq[string]$controller.pid){$controller}else{$currentTarget.identity};PhaseLive $target;$si.Environment['VI_PHASE_TARGET_PID']=[string]$target.pid;$si.Environment['VI_PHASE_TARGET_IMAGE']=$target.path;$si.Environment['VI_PHASE_TARGET_CREATED']=[string]([DateTime]::new($target.created,[DateTimeKind]::Utc).ToFileTimeUtc());$name='Local\ViPadPhaseCli_'+[guid]::NewGuid().ToString('N');$gate=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$name);$si.Environment['VI_PHASE_START_EVENT']=$name}
 $t=$null;$outTail=$null;$errTail=$null;$record=@{image=$Image;arguments=$Arguments;exit=$null;stdout='';stderr='';cleanup=$null};$ownedCalls.Add($record)
 try{
  PhaseRequire ($p.Start()) 'owned compiler/CLI started';$t=PhaseTrack $p $Image;$outTail=[ViPadPhaseTail]::new($p.StandardOutput);$errTail=[ViPadPhaseTail]::new($p.StandardError);PhaseValidate $t
  if($gate){PhaseRequire ($gate.Set()) 'ACK only after retained CLI identity'}
  PhaseRequire ($p.WaitForExit($Budget)) 'owned compiler/CLI bounded exit';$proof=Get-ViExitedCliAccounting $t.retained ([uint32]$t.pid) $t.created $Image $t.cache;PhaseRequire (Test-ViExitedCliAccounting $proof) 'original handle readonly exit accounting';$record.exit=[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$proof.native_exit_code),0);$record.exit_accounting=$proof
  $null=$outTail.Wait(500);$null=$errTail.Wait(500);$record.stdout=$outTail.Text();$record.stderr=$errTail.Text();PhaseRequire ($outTail.Complete-and$errTail.Complete-and-not$outTail.Error-and-not$errTail.Error) 'complete bounded separate CLI output';return $record
 }finally{try{PhaseFinish $t;$record.cleanup=if($t){$t.cleanup}else{$null}}finally{if($gate){$gate.Dispose()};if($t){$t.process.Dispose();$t.Remove('process')};$p.Dispose()}}
}
function PhaseStart($Target,[string]$Image,[bool]$NoRender=$false){
 $Image=(Resolve-Path -LiteralPath $Image).Path;$name='Local\ViPadPhaseTarget_'+[guid]::NewGuid().ToString('N');$gate=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$name)
 $Target.startGate=$gate
 $p=[Diagnostics.Process]::new();$p.StartInfo=[Diagnostics.ProcessStartInfo]::new();$p.StartInfo.FileName=$Image;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.WorkingDirectory=$tools
 [void]$p.StartInfo.ArgumentList.Add($Target.log);if($NoRender){[void]$p.StartInfo.ArgumentList.Add('--no-render')};[void]$p.StartInfo.ArgumentList.Add('--start-event='+$name)
 try{PhaseRequire ($p.Start()) 'owned target started';$Target.identity=PhaseTrack $p $Image;PhaseValidate $Target.identity;$Target.pid=$Target.identity.pid;PhaseRequire ($gate.Set()) 'ACK only after retained target identity';return $p}catch{$gate.Dispose();$Target.Remove('startGate');if(-not$Target.identity){$p.Dispose()};throw}
}
function PhaseStop($Target){
 if($Target-and$Target.ContainsKey('startGate')){$Target.startGate.Dispose();$Target.Remove('startGate')}
 if(-not$Target-or-not$Target.identity-or-not$Target.identity.ContainsKey('process')-or-not$Target.identity.process){return};$t=$Target.identity
 try{if(-not$t.process.HasExited-and$Target.hwnd){try{Control $Target 0x0010 0 -Closing}catch{$Target.close_error=$_.Exception.Message};$null=$t.process.WaitForExit(1500)}}finally{PhaseFinish $t;$Target.lifecycle=$t.cleanup;$t.process.Dispose();$t.Remove('process')}
}
function PhaseBuild([string]$Arch,[string]$Folder){
 New-Item -ItemType Directory -Force $Folder|Out-Null;$vc=if($Arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$hde=if($Arch-eq'x64'){'hde64'}else{'hde32'};$mh="$root\third_party\minhook";$batch=Join-Path $Folder 'build.cmd'
 @"
@echo off
call "$vcDir\$vc" >nul || exit /b 2
for %%S in (buffer hook trampoline) do (
cl /nologo /O2 /MT /W3 /I"$mh\include" /c "$mh\src\%%S.c" /Fo:"$Folder\lower_%%S.obj" || exit /b 2
cl /nologo /O2 /MT /W3 /I"$mh\include" /FI"$root\src\vi_mh_rename.h" /c "$mh\src\%%S.c" /Fo:"$Folder\private_%%S.obj" || exit /b 2
)
cl /nologo /O2 /MT /W3 /c "$mh\src\hde\$hde.c" /Fo:"$Folder\lower_hde.obj" || exit /b 2
cl /nologo /O2 /MT /W3 /FI"$root\src\vi_mh_rename.h" /c "$mh\src\hde\$hde.c" /Fo:"$Folder\private_hde.obj" || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /D_CRT_SECURE_NO_WARNINGS /std:c++17 /I"$root\src" /I"$mh\include" "$root\test\vi_pad_phaseconsumer.cpp" "$Folder\lower_buffer.obj" "$Folder\lower_hook.obj" "$Folder\lower_trampoline.obj" "$Folder\lower_hde.obj" /Fo:"$Folder\reader.obj" /Fe:"$Folder\reader.exe" /link /SUBSYSTEM:WINDOWS /PDB:NONE /INCREMENTAL:NO || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /WX /D_CRT_SECURE_NO_WARNINGS /std:c++20 /I"$root\src" /I"$mh\include" "$root\test\vi_padphase_guarded_cli.cpp" "$Folder\private_buffer.obj" "$Folder\private_hook.obj" "$Folder\private_trampoline.obj" "$Folder\private_hde.obj" /Fo:"$Folder\cli.obj" /Fe:"$Folder\vinput.exe" /link /SUBSYSTEM:CONSOLE /PDB:NONE /INCREMENTAL:NO
exit /b %errorlevel%
"@|Set-Content -LiteralPath $batch -Encoding ascii
 $record=PhaseRun $env:ComSpec @('/d','/c',$batch) 120000;[IO.File]::WriteAllText((Join-Path $Folder 'build.log'),$record.stdout+$record.stderr);PhaseRequire ($record.exit-eq0-and($record.stdout+$record.stderr)-notmatch'(?i)\bwarning\s+[A-Z]+\d+') 'guarded phase fixture/CLI compile without warnings';PhasePin "$Folder\reader.exe";PhasePin "$Folder\vinput.exe"
}
