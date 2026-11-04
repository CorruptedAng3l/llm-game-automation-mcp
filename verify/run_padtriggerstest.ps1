# Background actual XInputGetState/GetStateEx controls. The owned scoped reader
# derives from TRACKED test/vi_xinputtest.cpp; no inherited untracked code is used.
param([string]$Out="$PSScriptRoot\..\build\test\pad_triggers",[string]$Vinput='')
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot\..").Path
$V=if($Vinput){(Resolve-Path $Vinput).Path}else{(Resolve-Path "$root\dist\vinput.exe").Path}
$Out=[IO.Path]::GetFullPath($Out)
if(-not$Out.StartsWith("$root\",[StringComparison]::OrdinalIgnoreCase)-or-not$Out.StartsWith('E:\',[StringComparison]::OrdinalIgnoreCase)){throw 'Pad trigger output must stay inside this vinput copy on E:'}
New-Item -ItemType Directory -Force $Out|Out-Null
$run=[guid]::NewGuid().ToString('N');$tools="$Out\tools_$run";New-Item -ItemType Directory -Force "$tools\temp","$Out\calls"|Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$failures=0;$complete=$false;$owned=$null;$callNo=0;$errorDetail=$null
function Check($Good,$Name){$checks.Add(@{pass=[bool]$Good;name=$Name});Write-Host "$(if($Good){'PASS'}else{'FAIL'}) $Name";if(-not$Good){$script:failures++;throw $Name}}
if(-not('ViTriggerNative'-as[type])){Add-Type 'using System;using System.Runtime.InteropServices;public static class ViTriggerNative{[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);}' }
function Invoke-Vi([int]$TargetPid,[string[]]$Arguments,[int]$Budget=7000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$V;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in @('--json','--pid',[string]$TargetPid)+$Arguments){$info.ArgumentList.Add([string]$arg)}
 $proc=[Diagnostics.Process]::new();$proc.StartInfo=$info
 try{
  if(-not$proc.Start()){throw 'owned CLI did not start'};$stdout=$proc.StandardOutput.ReadToEndAsync();$stderr=$proc.StandardError.ReadToEndAsync()
  if(-not$proc.WaitForExit($Budget)){$proc.Kill();$proc.WaitForExit();throw 'owned CLI exceeded its bounded deadline'}
  $text=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult();$lines=@($text-split'\r?\n'|Where-Object{$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
  $script:callNo++;$record=@{arguments=$Arguments;exit=$proc.ExitCode;stdout=$text;stderr=$err;reply=$reply};$record|ConvertTo-Json -Depth 12|Set-Content "$Out\calls\$callNo.json" -Encoding utf8NoBOM
  Check ($lines.Count-eq1-and$null-ne$reply) "owned CLI has exactly one JSON reply for $($Arguments[0])";return $record
 }finally{$proc.Dispose()}
}
function Rows($Target){
 if(-not(Test-Path -LiteralPath $Target.log)){return @()};$index=0
 @(foreach($line in (Get-Content -LiteralPath $Target.log)){
  if($line-match'STATE src=(std|ex) rc=(\d+) pkt=(\d+) b=0x([0-9A-Fa-f]+) lt=(\d+) rt=(\d+) lx=(-?\d+) ly=(-?\d+) rx=(-?\d+) ry=(-?\d+)'){
   [pscustomobject]@{index=$index;source=$Matches[1];rc=[long]$Matches[2];packet=[long]$Matches[3];buttons=[Convert]::ToInt32($Matches[4],16);lt=[int]$Matches[5];rt=[int]$Matches[6];lx=[int]$Matches[7];ly=[int]$Matches[8];rx=[int]$Matches[9];ry=[int]$Matches[10];line=$line};$index++
  }
 })
}
function Is-State($Row,[int]$Buttons,[int]$Lt,[int]$Rt,[int]$Lx=0){$Row.rc-eq0-and$Row.buttons-eq$Buttons-and$Row.lt-eq$Lt-and$Row.rt-eq$Rt-and$Row.lx-eq$Lx-and$Row.ly-eq0-and$Row.rx-eq0-and$Row.ry-eq0}
function Await-State($Target,[int]$Start,[int]$Buttons,[int]$Lt,[int]$Rt,[int]$Lx=0,[int]$Budget=2500){
 $until=[Environment]::TickCount64+$Budget
 do{$rows=@(Rows $Target);$hits=@($rows|Where-Object{$_.index-ge$Start-and(Is-State $_ $Buttons $Lt $Rt $Lx)});if(@($hits|Where-Object source -eq std).Count-and@($hits|Where-Object source -eq ex).Count){return $hits};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until)
 throw "actual std+Ex state unavailable: b=$Buttons lt=$Lt rt=$Rt lx=$Lx; latest=$($rows|Select-Object -Last 4|ConvertTo-Json -Compress)"
}
function Observe-Timed($Target,$Record,[int]$Start,[int]$Buttons,[int]$Lt,[int]$Rt,$Name){
 Check ($Record.exit-eq0-and$Record.reply.ok-and$Record.reply.job_state-eq'completed') "$Name completed without command error"
 $pressed=@(Await-State $Target $Start $Buttons $Lt $Rt);$last=($pressed|Measure-Object index -Maximum).Maximum
 $neutral=@(Await-State $Target ($last+1) 0 0 0)
 $Target.evidence.Add(@{name=$Name;pressed=$pressed;released=$neutral;reply=$Record.reply})
 Check ($pressed.Count-ge2-and$neutral.Count-ge2) "$Name exact trigger press and later zero release reached std+Ex"
}
function Wait-Job($Target,$Id){$until=[Environment]::TickCount64+2500;do{$r=Invoke-Vi $Target.pid @('job-status',$Id);if($r.reply.job.job_complete){return $r.reply.job};Start-Sleep -Milliseconds 15}while([Environment]::TickCount64-lt$until);throw 'job did not become terminal'}
try{
 foreach($arch in @('x64','x86')){
  $vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'};$suffix=if($arch-eq'x64'){''}else{'32'}
  $batch=@"
@echo off
set "TEMP=$tools\temp"
set "TMP=$tools\temp"
call "$vcDir\$vc" >nul || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_padtriggerreader.cpp" /Fo:"$tools\reader$suffix.obj" /Fe:"$tools\reader$suffix.exe" /link /SUBSYSTEM:WINDOWS || exit /b 2
cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 "$root\test\vi_padbuttons_test.cpp" /Fo:"$tools\parser$suffix.obj" /Fe:"$tools\parser$suffix.exe" || exit /b 2
"@
  [IO.File]::WriteAllText("$tools\build_$arch.cmd",$batch,[Text.Encoding]::ASCII);& cmd /d /c "$tools\build_$arch.cmd" *> "$tools\build_$arch.log";Check ($LASTEXITCODE-eq0) "$arch owned derivative XInput reader and alias parser compile /W4 /WX"
  & "$tools\parser$suffix.exe" *> "$tools\parser_$arch.log";Check ($LASTEXITCODE-eq0) "$arch alias parser conflicts are atomic"
  $target=@{arch=$arch;pid=0;log="$Out\$arch.$run.target.log";evidence=[Collections.Generic.List[object]]::new()};$cases.Add($target)
  $owned=Start-Process "$tools\reader$suffix.exe" -ArgumentList "`"$($target.log)`" -28000 600 320 200 --dll xinput1_4.dll --ex" -WindowStyle Hidden -PassThru;$target.pid=$owned.Id
  try{
   $inject=Invoke-Vi $target.pid @('inject') 30000;Check ($inject.exit-eq0-and$inject.reply.ok) "$arch own target injection and real queue readiness"
   $status=Invoke-Vi $target.pid @('status');Check ($status.reply.ok-and-not$status.reply.really_foreground-and$status.reply.isolate) "$arch reader stays background and isolated"
   $start=@(Rows $target).Count;$r=Invoke-Vi $target.pid @('pad','none','300','--lt','128','--rt','17');Observe-Timed $target $r $start 0 128 17 "$arch legacy explicit partial trigger axes"
   $start=@(Rows $target).Count;$r=Invoke-Vi $target.pid @('pad','a+LT+rt','300');Observe-Timed $target $r $start 0x1000 255 255 "$arch CLI mixed aliases"
   $start=@(Rows $target).Count;$wire=@{cmd='pad';buttons='b+rt';lt=128;ms=300}|ConvertTo-Json -Compress;$r=Invoke-Vi $target.pid @('json',$wire);Observe-Timed $target $r $start 0x2000 128 255 "$arch direct JSON alias plus other partial trigger"
   $start=@(Rows $target).Count;$r=Invoke-Vi $target.pid @('pad','rb+lt','--rt','64','--hold');Check ($r.exit-eq0-and$r.reply.ok) "$arch indefinite trigger alias hold accepted";$down=@(Await-State $target $start 0x200 255 64);$target.evidence.Add(@{name='indefinite alias hold';rows=$down})
   $releaseStart=@(Rows $target).Count;Invoke-Vi $target.pid @('release')|Out-Null;Await-State $target $releaseStart 0 0 0|Out-Null
   $start=@(Rows $target).Count;$r=Invoke-Vi $target.pid @('pad','lt+rt','4000','--async');Check ($r.reply.accepted-and$r.reply.job_id) "$arch cancellable dual-trigger job admitted";Await-State $target $start 0 255 255|Out-Null
   $releaseStart=@(Rows $target).Count;$cancel=Invoke-Vi $target.pid @('job-cancel',$r.reply.job_id);$terminal=Wait-Job $target $r.reply.job_id;Await-State $target $releaseStart 0 0 0|Out-Null;Check ($cancel.reply.ok-and$terminal.job_state-eq'cancelled') "$arch cancellation releases both trigger aliases exactly"
   $start=@(Rows $target).Count;$old=Invoke-Vi $target.pid @('pad','lt','4000','--async');Await-State $target $start 0 255 0|Out-Null
   $start=@(Rows $target).Count;$new=Invoke-Vi $target.pid @('pad','rt','--hold');Await-State $target $start 0 0 255|Out-Null;$terminal=Wait-Job $target $old.reply.job_id;Check ($new.reply.ok-and$terminal.job_state-eq'cancelled') "$arch newer RT hold survives LT job cancellation";Invoke-Vi $target.pid @('release')|Out-Null
   $start=@(Rows $target).Count;$r=Invoke-Vi $target.pid @('pad-ramp','lx','0','5000','300','--buttons','a+lt','--pacing','time','--end-hold-ms','100');$pressed=@(Await-State $target $start 0x1000 255 0 5000);$last=($pressed|Measure-Object index -Maximum).Maximum;Await-State $target ($last+1) 0 0 0|Out-Null;Check ($r.reply.ok) "$arch ramp button-list alias reaches endpoint and releases"
   $states='[{"at_ms":0,"buttons":"a+rt"},{"at_ms":300,"buttons":"none"}]';$wire=@{cmd='pad-timeline';states_json=$states;pacing='time';ms=450}|ConvertTo-Json -Compress;$start=@(Rows $target).Count;$r=Invoke-Vi $target.pid @('json',$wire);Observe-Timed $target $r $start 0x1000 0 255 "$arch timeline trigger alias"
   $start=@(Rows $target).Count;Invoke-Vi $target.pid @('pad','none','--lt','64','--rt','32','--lx','321','--hold')|Out-Null;Await-State $target $start 0 64 32 321|Out-Null
   $invalids=@(
    @{name='alias plus partial same trigger';argv=@('pad','a+lt','300','--lt','128')},
    @{name='alias plus explicit255 same trigger';argv=@('pad','lt','300','--lt','255')},
    @{name='invalid suffix';argv=@('pad','lt+unknown','300')},
    @{name='bare trigger option';argv=@('pad','a+rt','--lt')},
    @{name='misspelled trigger option';argv=@('pad','a+lt','300','--lt128')},
    @{name='duplicate trigger options';argv=@('pad','lt','--rt','1','--rt','2','--hold')},
    @{name='extra positional argument';argv=@('pad','lt','300','extra')},
    @{name='hold plus duration';argv=@('pad','a+lt','300','--hold')},
    @{name='JSON alias conflict';argv=@('json','{"cmd":"pad","buttons":"a+rt","rt":128,"ms":300}')},
    @{name='JSON invalid button type';argv=@('json','{"cmd":"pad","buttons":true,"ms":300}')},
    @{name='JSON unknown axis field';argv=@('json','{"cmd":"pad","buttons":"a+lt","lt128":255,"ms":300}')},
    @{name='JSON invalid axis type';argv=@('json','{"cmd":"pad","buttons":"lt","rt":"nonsense","ms":300}')},
    @{name='JSON nonfinite axis';argv=@('json','{"cmd":"pad","buttons":"a+lt","rt":1e309,"ms":300}')},
    @{name='JSON duplicate buttons';argv=@('json','{"cmd":"pad","buttons":"lt","buttons":"a","ms":300}')},
    @{name='JSON duplicate trigger';argv=@('json','{"cmd":"pad","buttons":"lt","rt":1,"rt":2,"ms":300}')},
    @{name='ramp alias versus ramp axis';argv=@('pad-ramp','lt','0','128','300','--buttons','lt','--pacing','time')},
    @{name='ramp alias versus companion axis';argv=@('pad-ramp','lx','0','5000','300','--lt','128','--buttons','lt','--pacing','time')}
   )
   foreach($state in @('[{"at_ms":0,"buttons":"lt","lt":128}]','[{"at_ms":0,"lt":255,"buttons":"lt"}]')){$invalids+=@{name="timeline alias conflict $state";argv=@('json',(@{cmd='pad-timeline';states_json=$state;pacing='time';ms=300}|ConvertTo-Json -Compress))}}
   foreach($invalid in $invalids){
    $before=@(Rows $target);$baseline=@($before|Group-Object source|ForEach-Object{$_.Group|Select-Object -Last 1});$r=Invoke-Vi $target.pid $invalid.argv
    Check ($r.exit-ne0-and-not$r.reply.ok-and-not$r.reply.accepted) "$arch $($invalid.name) rejected before admission"
    $freshStart=@(Rows $target).Count;Await-State $target $freshStart 0 64 32 321|Out-Null
    $after=@(Rows $target);$changed=@($after|Where-Object{$_.index-ge$before.Count-and-not(Is-State $_ 0 64 32 321)})
    $latest=@($after|Group-Object source|ForEach-Object{$_.Group|Select-Object -Last 1})
    Check ($baseline.Count-eq2-and$latest.Count-eq2-and$changed.Count-eq0-and@($latest|Where-Object{-not(Is-State $_ 0 64 32 321)}).Count-eq0-and@($latest|Where-Object{$row=$_;@($baseline|Where-Object{$_.source-eq$row.source-and$_.packet-eq$row.packet}).Count-eq0}).Count-eq0) "$arch $($invalid.name) preserves exact existing actual state and packet"
    $target.evidence.Add(@{name=$invalid.name;reply=$r.reply;before=$baseline;after=$latest;changed=$changed})
   }
   Invoke-Vi $target.pid @('release')|Out-Null;$status=Invoke-Vi $target.pid @('status');[uint32]$foregroundPid=0;[ViTriggerNative]::GetWindowThreadProcessId([ViTriggerNative]::GetForegroundWindow(),[ref]$foregroundPid)|Out-Null
   Check (-not$status.reply.really_foreground-and$foregroundPid-ne$target.pid) "$arch all alias controls preserve real background ownership"
  }finally{if($owned-and-not$owned.HasExited){try{Invoke-Vi $target.pid @('release')|Out-Null}catch{};$owned.Kill();$owned.WaitForExit()};if($owned){$owned.Dispose();$owned=$null}}
 }
 $complete=$true
}catch{
 $errorDetail=$_.Exception.Message;if(-not$failures){$failures++;$checks.Add(@{pass=$false;name='bounded fixture/command observation';detail=$errorDetail});Write-Host "FAIL bounded fixture/command observation: $errorDetail"};throw
}finally{
 if($owned-and-not$owned.HasExited){$owned.Kill();$owned.WaitForExit()};if($owned){$owned.Dispose()}
 @{schema=1;status=$(if($failures-or-not$complete){'failed'}else{'passed'});completed=$complete;failed_check_count=$failures;error=$errorDetail;checks=$checks;cases=$cases;reader_source='own test/vi_padtriggerreader.cpp derived from tracked test/vi_xinputtest.cpp';tools=$tools}|ConvertTo-Json -Depth 15|Set-Content "$Out\result.json" -Encoding utf8NoBOM
}
"$($checks.Count-$failures) PASS / $failures FAIL";if($failures){exit 1};exit 0
