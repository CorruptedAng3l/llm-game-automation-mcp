# New-CLI / explicitly-selected-DLL capture-space compatibility. No expected-red inversion.
# Uses the CURRENT tracked safe fixture without changing it or its safety header.
param([Parameter(Mandatory)][string]$CliDir,[Parameter(Mandatory)][string]$DllDir,
      [string]$RepoRoot='', [string]$Out="$PSScriptRoot\..\build\test\capture_scope_compat\after",[switch]$RequireScopeSupport)
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.Drawing
if(-not$RepoRoot){$candidate=[IO.DirectoryInfo]::new((Resolve-Path "$PSScriptRoot\..").Path);for($n=0;$n-lt8-and$candidate;$n++){if(Test-Path (Join-Path $candidate.FullName 'build\x64\vimh_buffer.obj')){$RepoRoot=$candidate.FullName;break};$candidate=$candidate.Parent}}
if(-not$RepoRoot){throw 'Pass -RepoRoot containing tracked test/vi_capturescope.cpp and tested VIMH objects'}
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path;$CliDir=(Resolve-Path -LiteralPath $CliDir).Path;$DllDir=(Resolve-Path -LiteralPath $DllDir).Path;$V=(Resolve-Path -LiteralPath (Join-Path $CliDir 'vinput.exe')).Path
$source=(Resolve-Path -LiteralPath (Join-Path $RepoRoot 'test\vi_capturescope.cpp')).Path;$safety=(Resolve-Path -LiteralPath (Join-Path $RepoRoot 'test\vi_cursor_spies.h')).Path
$dlls=@{x64=(Resolve-Path -LiteralPath (Join-Path $DllDir 'vinput64.dll')).Path;x86=(Resolve-Path -LiteralPath (Join-Path $DllDir 'vinput32.dll')).Path}
$run=Join-Path $Out ('run_'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force -Path $run|Out-Null;$run=(Resolve-Path -LiteralPath $run).Path
$checks=[Collections.Generic.List[object]]::new();$calls=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new();$token=0;$complete=$false;$exception=$null;$incompleteArchitectures=[Collections.Generic.List[string]]::new()
if(-not('ViCaptureScopeCompatNative'-as[type])){Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ViCaptureScopeCompatNative{
 [DllImport("user32.dll",SetLastError=true)]public static extern bool PostMessageW(IntPtr h,uint m,UIntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
}
'@}
function Check([bool]$ok,[string]$name){$checks.Add([pscustomobject]@{pass=$ok;name=$name});Write-Host "$(if($ok){'PASS'}else{'FAIL'}) $name"}
function Require([bool]$ok,[string]$name){Check $ok $name;if(-not$ok){throw $name}}
function Text($t){if(-not(Test-Path -LiteralPath $t.log)){return ''};$stream=[IO.FileStream]::new($t.log,'Open','Read',([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));$reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose();$stream.Dispose()}}
function Rows($t){foreach($line in ((Text $t)-split'\r?\n')){if($line.StartsWith('{')-and$line.EndsWith('}')){$line|ConvertFrom-Json}}}
function Await($t,[string]$event,[uint64]$value=0){$until=[Environment]::TickCount64+3500;do{foreach($r in @(Rows $t)){if($r.event-eq$event-and($value-eq0-or$r.token-eq$value)){return $r}};if($t.process.HasExited){throw ('owned fixture exited0x'+[BitConverter]::ToUInt32([BitConverter]::GetBytes([int32]$t.process.ExitCode),0).ToString('X8'))};Start-Sleep -Milliseconds 10}while([Environment]::TickCount64-lt$until);throw "missing $event token$value"}
function Control($t,[uint32]$message,[string]$event){if($t.process.HasExited-or-not(Live-Owned $t.identity)){throw 'owned fixture process lifetime ended or changed'};[uint32]$owner=0;$thread=[ViCaptureScopeCompatNative]::GetWindowThreadProcessId($t.control,[ref]$owner);if(-not$thread-or$owner-ne$t.process.Id-or$thread-ne$t.thread-or-not[ViCaptureScopeCompatNative]::IsWindow($t.control)){throw 'owned control HWND/PID/thread changed'};$script:token++;if($t.process.HasExited-or-not(Live-Owned $t.identity)){throw 'owned fixture process lifetime changed before post'};if(-not[ViCaptureScopeCompatNative]::PostMessageW($t.control,$message,[UIntPtr]::new([uint64]$script:token),[IntPtr]::Zero)){throw 'owned control PostMessage failed'};return Await $t $event $script:token}
function Snap($t){return Control $t 0x8050 snapshot}
function Real-Foreground(){[uint32]$p=0;$window=[ViCaptureScopeCompatNative]::GetForegroundWindow();[void][ViCaptureScopeCompatNative]::GetWindowThreadProcessId($window,[ref]$p);return [pscustomobject]@{hwnd=$window.ToInt64();pid=$p;read_only=$true}}
function Live-Owned($identity){try{$p=Get-Process -Id $identity.process.Id -ErrorAction SilentlyContinue;if($p-and$p.Path-eq$identity.path-and$p.StartTime.ToUniversalTime().Ticks-eq$identity.created){return $p}}catch{};return $null}
function Stop-Owned($identity){$p=Live-Owned $identity;if($p){$identity.process.Kill();if(-not$identity.process.WaitForExit(1500)){throw 'owned fixture did not exit within bounded cleanup'}}}
function Run-Bounded([string]$file,[string[]]$arguments,[int]$budget=6000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$file;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;foreach($argument in $arguments){[void]$info.ArgumentList.Add($argument)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;$watch=[Diagnostics.Stopwatch]::StartNew();if(-not$p.Start()){throw 'owned CLI/compiler start failed'}
 $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$expired=-not$p.WaitForExit($budget);$reaped=$true
 if($expired-and-not$p.HasExited){$p.Kill();$reaped=$p.WaitForExit(1500)}
 $outText=if($stdout.Wait(750)){$stdout.Result}else{'<stdout unavailable after deadline>'};$errText=if($stderr.Wait(750)){$stderr.Result}else{'<stderr unavailable after deadline>'};$code=if($p.HasExited){$p.ExitCode}else{-1};$watch.Stop();$p.Dispose()
 return [pscustomobject]@{exit=$code;stdout=$outText;stderr=$errText;timeout=$expired;owned_reaped=$reaped;elapsed_ms=$watch.ElapsedMilliseconds}
}
function Call($t,[string[]]$arguments,[int]$budget=6000){$prefix=@('--json','--pid',"$($t.process.Id)");if($t.owner){$prefix+=@('--owner',$t.owner)};$r=Run-Bounded $V ($prefix+$arguments) $budget;$lines=@($r.stdout-split'\r?\n'|Where-Object {$_-match'\S'});$reply=$null;if($lines.Count-eq1){try{$reply=$lines[0]|ConvertFrom-Json}catch{}}
 $item=[pscustomobject]@{arch=$t.arch;pid=$t.process.Id;arguments=$arguments;global_owner=$t.owner;exit=$r.exit;stdout=$r.stdout;stderr=$r.stderr;timeout=$r.timeout;owned_reaped=$r.owned_reaped;elapsed_ms=$r.elapsed_ms;reply=$reply};$calls.Add($item)
 Require (-not$r.timeout-and$r.owned_reaped-and$lines.Count-eq1-and$null-ne$reply) "$($t.arch) bounded owned CLI emits one JSON reply";return $item
}
function Json($t,$value){return Call $t @('json',($value|ConvertTo-Json -Compress -Depth 8))}
function Trace($t,[string]$key='lbutton'){$r=Call $t @('trace','key',$key);Require ($r.exit-eq0-and$r.reply.ok) "$($t.arch) fresh $key trace";return $r.reply}
function No-Position($before,$after){return $before.amoves-eq$after.amoves-and$before.bmoves-eq$after.bmoves-and$before.adown-eq$after.adown-and$before.bdown-eq$after.bdown}
function Scope-Reason($reply){return [bool]($reply.capture_scope_reason-or$reply.down.capture_scope_reason-or$reply.job.capture_scope_reason)}
function Refusal($t,[string]$name,[string[]]$arguments,[switch]$LocalJson){
 $before=Snap $t;$traceBefore=Trace $t;$statusBefore=Call $t @('status');$r=Call $t $arguments
 if($r.reply.job_id){Start-Sleep -Milliseconds 150;[void](Call $t @('job-cancel',"$($r.reply.job_id)"))}
 $after=Snap $t;$traceAfter=Trace $t;$statusAfter=Call $t @('status')
 $stopped=$r.exit-ne0-and-not$r.reply.ok-and(No-Position $before $after)-and$traceAfter.injected_down-eq$traceBefore.injected_down
 if($LocalJson){$stopped=$stopped-and$r.reply.capture_scope_preflight-and$r.reply.capture_scope_support_checked-eq$false-and$r.reply.input_injected-eq$false-and$r.reply.coordinate_input_sent-eq$false-and$r.reply.button_down_count-eq0}
 elseif(-not$RequireScopeSupport){$stopped=$stopped-and$r.reply.capture_scope_support_checked-and$r.reply.coordinate_input_sent-eq$false-and$r.reply.button_down_count-eq0}else{$stopped=$stopped-and(Scope-Reason $r.reply)}
 if($statusBefore.reply.cursor_guard-and$statusAfter.reply.cursor_guard-and$statusBefore.reply.cursor_guard.PSObject.Properties['agent_cursor_epoch']-and$statusAfter.reply.cursor_guard.PSObject.Properties['agent_cursor_epoch']){$stopped=$stopped-and$statusBefore.reply.cursor_guard.agent_cursor_epoch-eq$statusAfter.reply.cursor_guard.agent_cursor_epoch}
 Check $stopped "$($t.arch) $name refuses before any fixture move/down or trace injection"
 $evidence.Add([pscustomobject]@{arch=$t.arch;name=$name;before=$before;after=$after;trace_before=$traceBefore;trace_after=$traceAfter;status_before=$statusBefore.reply;status_after=$statusAfter.reply;call=$r})
 [void](Call $t @('mouseup','left'));return $r
}
function Capture($t,[string]$name){$path=Join-Path $run ("$($t.arch).$name.png");$r=Call $t @('capture',$path,'--method','printwindow');Require ($r.exit-eq0-and$r.reply.ok-and(Test-Path -LiteralPath $path)) "$($t.arch) plain actual PNG capture succeeds with selected DLL"
 $bitmap=[Drawing.Bitmap]::new($path);try{$pixel=$bitmap.GetPixel(5,5);$png=[pscustomobject]@{w=$bitmap.Width;h=$bitmap.Height;r=$pixel.R;g=$pixel.G;b=$pixel.B}}finally{$bitmap.Dispose()}
 $evidence.Add([pscustomobject]@{arch=$t.arch;name=$name;call=$r;png=$png;capture_backend='owned lower PrintWindow memory-DC surrogate';native_printwindow_measured=$false})
 Check ($png.w-eq$r.reply.w-and$png.h-eq$r.reply.h-and$png.r-eq220-and$png.g-eq160-and$png.b-eq40) "$($t.arch) independently decoded PNG matches deterministic fixture";return $r
}
try {
 foreach($arch in @('x64','x86')){
  $folder=Join-Path $run "tools\$arch";New-Item -ItemType Directory -Force -Path $folder,(Join-Path $folder 'temp')|Out-Null;$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  foreach($object in @('vimh_buffer.obj','vimh_hook.obj','vimh_trampoline.obj','vimh_hde.obj')){if(-not(Test-Path (Join-Path $RepoRoot "build\$arch\$object"))){throw "missing tested $arch $object"}}
  $build=Join-Path $folder 'build.cmd';@"
@echo off
set "TEMP=$folder\temp"
set "TMP=$folder\temp"
call "$vcDir\$vc" >nul
if errorlevel 1 exit /b 1
cd /d "$folder"
cl /nologo /O2 /MT /EHsc /std:c++17 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"$RepoRoot\src" /I"$RepoRoot\third_party\minhook\include" "$source" /Fe:fixture.exe /link /SUBSYSTEM:WINDOWS "$RepoRoot\build\$arch\vimh_buffer.obj" "$RepoRoot\build\$arch\vimh_hook.obj" "$RepoRoot\build\$arch\vimh_trampoline.obj" "$RepoRoot\build\$arch\vimh_hde.obj"
exit /b %errorlevel%
"@|Set-Content -LiteralPath $build -Encoding ascii
  $compiled=Run-Bounded $env:ComSpec @('/d','/c',$build) 30000;($compiled.stdout+$compiled.stderr)|Set-Content (Join-Path $folder 'build.log');Require (-not$compiled.timeout-and$compiled.owned_reaped-and$compiled.exit-eq0-and($compiled.stdout+$compiled.stderr)-notmatch'(?im)\bwarning (?:C|LNK)\d+') "$arch current tracked safe fixture builds /W4 /WX"
 }
 foreach($arch in @('x64','x86')){
  $exe=(Resolve-Path (Join-Path $run "tools\$arch\fixture.exe")).Path;$log=Join-Path $run ($arch+'.jsonl');$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$exe;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;[void]$info.ArgumentList.Add($log)
  $p=[Diagnostics.Process]::Start($info);$identity=[pscustomobject]@{process=$p;path=$exe;created=$p.StartTime.ToUniversalTime().Ticks};$owned.Add($identity);$t=[pscustomobject]@{arch=$arch;process=$p;identity=$identity;log=$log;control=[IntPtr]::Zero;thread=[uint32]0;owner=''}
  try {
   $ready=Await $t ready;Require ($ready.pid-eq$p.Id-and$ready.safe_spies-and$ready.native_write_trampolines-eq$false-and$ready.owned_print_surrogate-and$ready.native_printwindow_called-eq$false) "$arch owned fixture safety and backend ready"
   $t.control=[IntPtr]::new([long][uint64]$ready.primary);[uint32]$pidValue=0;$t.thread=[ViCaptureScopeCompatNative]::GetWindowThreadProcessId($t.control,[ref]$pidValue);Require ($pidValue-eq$p.Id-and$t.thread-ne0) "$arch exact control HWND/PID/thread identity"
   $foregroundBefore=Real-Foreground;Require ($foregroundBefore.pid-ne$p.Id) "$arch actual foreground outside fixture"
   $inject=Call $t @('inject','--dll',$dlls[$arch],'--hooks-only') 12000;Require ($inject.exit-eq0-and$inject.reply.ok) "$arch explicitly selected DLL injection succeeds"
   $config=Call $t @('config',('hwnd=0x{0:X}'-f[uint64]$ready.primary),'isolate=true','fakefocus=true','msgs=on','raw=off','blockfocus=true');Require ($config.exit-eq0-and$config.reply.ok) "$arch exact primary adoption"
   $arm=Call $t @('readiness','--wait-ms','1500');Require ($arm.exit-eq0-and$arm.reply.ok-and$arm.reply.queue_ready) "$arch fresh queue readiness"
   $ping=Json $t @{cmd='ping';capture_scope_support=$true};$status=Call $t @('status');$support=$ping.reply.capture_scope_version-ge1-and$status.reply.capture_scope_version-ge1
   Require ($support-eq[bool]$RequireScopeSupport) "$arch selected DLL matches requested capture-scope capability mode"
   $r=Json $t @{cmd='mouse';x=0.5;y=0.5;norm=$true;space='capture'};$a=Snap $t;Check ($r.exit-eq0-and$r.reply.ok-and$a.ax-eq320-and$a.ay-eq240) "$arch norm=true takes precedence even before any saved capture"
   $saved=Capture $t initial;$resize=Control $t 0x8051 resize;Require ($resize.ok-and$resize.aw-eq800-and$resize.ah-eq600-and$saved.reply.w-eq640-and$saved.reply.h-eq480) "$arch owned geometry change makes initial map stale"
   # A scaled/cropped stale map makes implicit-coordinate consumption observable:
   # current client20,15 would become120,90 through the retained crop transform.
   $implicitPath=Join-Path $run ("$arch.implicit_crop.png")
   $implicitCrop=Call $t @('capture',$implicitPath,'--method','printwindow','--region','80,60,320,240','--scale','0.5')
   Require ($implicitCrop.exit-eq0-and$implicitCrop.reply.ok-and$implicitCrop.reply.w-eq160-and$implicitCrop.reply.h-eq120-and$implicitCrop.reply.origin_x-eq80-and$implicitCrop.reply.origin_y-eq60-and$implicitCrop.reply.region_client_w-eq320-and$implicitCrop.reply.region_client_h-eq240) "$arch explicit crop/scale gives a nonidentity implicit-coordinate map"
   $implicitResize=Control $t 0x8058 reset_size;Require ($implicitResize.ok-and$implicitResize.aw-eq640-and$implicitResize.ah-eq480) "$arch actual resize makes cropped map stale"
   foreach($motion in @('mouse','move')){
    $position=Call $t @('mouse','20','15');Require ($position.exit-eq0-and$position.reply.ok-and$position.reply.x-eq20-and$position.reply.y-eq15) "$arch $motion implicit control starts at client20,15"
    $request=@{cmd=$motion;space='capture'}|ConvertTo-Json -Compress
    $null=Refusal $t ($motion+'_implicit_capture') @('json',$request)
   }
   $relativeCases=[Collections.Generic.List[object]]::new()
   $relativeCases.Add(@{name='mouse_dx_only';payload=@{cmd='mouse';dx=3;space='capture'};x=23;y=15})
   $relativeCases.Add(@{name='move_dy_only';payload=@{cmd='move';dy=-2;space='capture'};x=20;y=13})
   foreach($motion in @('mouse','move')){foreach($withXY in @($false,$true)){foreach($withNorm in @($false,$true)){
    $payload=@{cmd=$motion;dx=3;dy=-2;space='capture'}
    if($withXY){$payload.x=999;$payload.y=999};if($withNorm){$payload.norm=$true}
    $relativeCases.Add(@{name=($motion+'_relative_xy'+$withXY+'_norm'+$withNorm);payload=$payload;x=23;y=13})
   }}}
   foreach($relative in $relativeCases){
    $position=Call $t @('mouse','20','15');Require ($position.exit-eq0-and$position.reply.ok) "$arch $($relative.name) establishes client start"
    $beforeRelative=Snap $t;$r=Json $t $relative.payload;$afterRelative=Snap $t
    Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.x-eq$relative.x-and$r.reply.y-eq$relative.y-and$afterRelative.ax-eq$relative.x-and$afterRelative.ay-eq$relative.y-and$afterRelative.adown-eq$beforeRelative.adown-and$afterRelative.bdown-eq$beforeRelative.bdown) "$arch $($relative.name) uses unscaled device deltas; norm/capture-space do not transform relative input"
    $evidence.Add([pscustomobject]@{arch=$arch;name=$relative.name;request=$relative.payload;before=$beforeRelative;after=$afterRelative;call=$r;relative_scope='device deltas; no screenshot mapping or normalized scaling'})
   }
   # Restore the original full-capture/stale800x600 setup for all existing cases.
   $null=Capture $t restored_full_before_stale;$restoreResize=Control $t 0x8051 resize;Require ($restoreResize.ok-and$restoreResize.aw-eq800-and$restoreResize.ah-eq600) "$arch original stale full-map setup restored"
   $null=Refusal $t mouse @('mouse','40','30','--capture-space')
   $null=Refusal $t click @('click','left','40','30','--capture-space','--hover-frames','0','--settle-ms','0','--ms','1','--di-poll-timeout-ms','0')
   $null=Refusal $t drag @('drag','40','30','80','60','--capture-space','--pace','time','--ms','40')
   $null=Refusal $t placement @('placement-drag','40','30','80','60','--capture-space','--steps','3','--ms','40','--dwell-ms','0','--poll-timeout-ms','100','--async')
   $null=Refusal $t escaped_space @('json','{"cmd":"mouse","x":40,"y":30,"\u0073pace":"cap\u0074ure"}')
   $null=Refusal $t duplicate_local @('json','{"cmd":"mouse","x":40,"y":30,"space":"client","space":"capture"}') -LocalJson
   $null=Refusal $t malformed_local @('json','{"cmd":"mouse","x":40,"y":30,"space":"capture"') -LocalJson
   foreach($norm in @($true,1,-1,'true','1','on')){
    $r=Json $t @{cmd='mouse';x=0.5;y=0.5;space='capture';norm=$norm};$a=Snap $t;Check ($r.exit-eq0-and$r.reply.ok-and$r.reply.x-eq400-and$r.reply.y-eq300-and$a.ax-eq400-and$a.ay-eq300) "$arch JBool norm precedence preserved for $norm ($($norm.GetType().Name))"
   }
   foreach($norm in @($false,0,'off')){$request=@{cmd='mouse';x=40;y=30;space='capture';norm=$norm}|ConvertTo-Json -Compress;$null=Refusal $t ('false_norm_'+[string]$norm) @('json',$request)}
   $hold=Call $t @('mousedown','right');$held=Trace $t rbutton;Require ($hold.exit-eq0-and$hold.reply.ok-and$held.virtual_down) "$arch right hold positively precedes positioned-release refusal"
   $before=Snap $t;$up=Call $t @('mouseup','right','40','30','--capture-space');$after=Snap $t;$released=Trace $t rbutton
   $upOk=$up.exit-ne0-and-not$up.reply.ok-and(No-Position $before $after)-and$after.aup-gt$before.aup-and-not$released.virtual_down-and$released.injected_up-gt$held.injected_up
   if(-not$RequireScopeSupport){$upOk=$upOk-and$up.reply.capture_scope_support_checked-and$up.reply.coordinate_input_sent-eq$false-and$up.reply.button_down_count-eq0-and$up.reply.cleanup_attempted-and$up.reply.cleanup_acknowledged-and$up.reply.cleanup_raw}else{$upOk=$upOk-and$up.reply.capture_scope_reason-and$up.reply.button_released}
   Check $upOk "$arch positioned mouseup refuses coordinates and observed coordinate-free cleanup actually releases"
   $evidence.Add([pscustomobject]@{arch=$arch;name='positioned_mouseup_cleanup';before=$before;after=$after;held_trace=$held;released_trace=$released;call=$up})
   $hold=Call $t @('mousedown','right');$held=Trace $t rbutton;Require ($hold.reply.ok-and$held.virtual_down) "$arch second positive right hold"
   $before=Snap $t;$up=Call $t @('mouseup','right');$after=Snap $t;$released=Trace $t rbutton
   Check ($up.exit-eq0-and$up.reply.ok-and-not$up.reply.capture_scope_support_checked-and(No-Position $before $after)-and$after.aup-gt$before.aup-and-not$released.virtual_down-and$released.injected_up-gt$held.injected_up) "$arch ordinary coordinate-free mouseup is never capability gated"
   $t.owner='capture-scope-compat-'+$arch+'-'+[guid]::NewGuid().ToString('N')
   try {
    $claim=Call $t @('claim','--ttl','30');Require ($claim.exit-eq0-and$claim.reply.ok) "$arch unique global owner claims only the owned target"
    $hold=Call $t @('mousedown','right');$held=Trace $t rbutton;Require ($hold.exit-eq0-and$hold.reply.ok-and$held.virtual_down) "$arch active claimant positively holds right"
    $before=Snap $t;$up=Call $t @('mouseup','right','40','30','--capture-space');$after=Snap $t;$released=Trace $t rbutton
    $ownerCleanup=$up.exit-ne0-and-not$up.reply.ok-and(No-Position $before $after)-and$after.aup-gt$before.aup-and-not$released.virtual_down-and$released.injected_up-gt$held.injected_up
    if(-not$RequireScopeSupport){$ownerCleanup=$ownerCleanup-and$up.reply.capture_scope_support_checked-and$up.reply.coordinate_input_sent-eq$false-and$up.reply.cleanup_attempted-and$up.reply.cleanup_acknowledged}else{$ownerCleanup=$ownerCleanup-and$up.reply.capture_scope_reason-and$up.reply.button_released}
    Check $ownerCleanup "$arch same global owner is preserved through coordinate-free refusal cleanup"
    $evidence.Add([pscustomobject]@{arch=$arch;name='active_claim_cleanup';global_owner=$t.owner;claim=$claim;held_trace=$held;released_trace=$released;before=$before;after=$after;call=$up})
   } finally {
    try{[void](Call $t @('mouseup','right'))}catch{Check $false "$arch bounded active-claim release cleanup: $($_.Exception.Message)"}
    try{$unclaim=Call $t @('unclaim');Check ($unclaim.exit-eq0-and$unclaim.reply.ok) "$arch owned active claim is removed"}catch{Check $false "$arch bounded active-claim unclaim: $($_.Exception.Message)"}
    $t.owner=''
   }
   $fresh=Capture $t after_resize
   if($RequireScopeSupport){$before=Snap $t;$click=Call $t @('click','left','40','30','--capture-space','--hover-frames','0','--settle-ms','0','--ms','1','--di-poll-timeout-ms','0');$after=Snap $t;Check ($click.exit-eq0-and$click.reply.ok-and$after.adown-eq$before.adown+1-and$after.ax-eq40-and$after.ay-eq30) "$arch fresh map with supported DLL permits capture-space input"}else{$null=Refusal $t fresh_but_unavailable @('mouse','40','30','--capture-space')}
   $foregroundAfter=Real-Foreground;Check ($foregroundAfter.pid-ne$p.Id) "$arch actual foreground remains outside fixture"
   $evidence.Add([pscustomobject]@{arch=$arch;dll=$dlls[$arch];dll_sha256=(Get-FileHash $dlls[$arch] -Algorithm SHA256).Hash;ping=$ping.reply;status=$status.reply;selected_scope_support=$support;foreground_before=$foregroundBefore;foreground_after=$foregroundAfter;final_snapshot=(Snap $t);log=$log})
  } catch {$incompleteArchitectures.Add($arch);$evidence.Add([pscustomobject]@{arch=$arch;error=$_.Exception.Message;exit_code=$(if($p.HasExited){$p.ExitCode}else{$null});last_records=@(Rows $t|Select-Object -Last 5);log=$log});if(-not$checks.Count-or$checks[$checks.Count-1].pass){Check $false "$arch setup/runtime error: $($_.Exception.Message)"}}
  finally {Stop-Owned $identity}
 }
 $complete=$incompleteArchitectures.Count-eq0
} catch {$exception=$_.Exception.Message;if(-not$checks.Count-or$checks[$checks.Count-1].pass){Check $false ('runner error: '+$exception)}}
finally {
 foreach($identity in $owned){try{Stop-Owned $identity}catch{Check $false ('bounded owned cleanup: '+$_.Exception.Message)}};$failed=@($checks|Where-Object {-not$_.pass}).Count
 $checks|ConvertTo-Json -Depth 12|Set-Content (Join-Path $run 'checks.json');$calls|ConvertTo-Json -Depth 30|Set-Content (Join-Path $run 'calls.json');$evidence|ConvertTo-Json -Depth 40|Set-Content (Join-Path $run 'evidence.json')
 [pscustomobject]@{completed=$complete;passed=$checks.Count-$failed;failed=$failed;error=$exception;out_dir=$run;expected_red=$false;require_scope_support=[bool]$RequireScopeSupport;cli=$V;cli_sha256=(Get-FileHash $V -Algorithm SHA256).Hash;dll_dir=$DllDir;tracked_fixture=$source;fixture_sha256=(Get-FileHash $source -Algorithm SHA256).Hash;safety_sha256=(Get-FileHash $safety -Algorithm SHA256).Hash;capture_backend='owned lower PrintWindow memory-DC surrogate';native_printwindow_measured=$false;real_game_measured=$false;physical_input_generated=$false;tool_issues=@()}|ConvertTo-Json -Depth 12|Set-Content (Join-Path $run 'result.json')
}
Write-Host "$($checks.Count-$failed) PASS / $failed FAIL ($run)";if($failed-or-not$complete){exit 1};exit 0
