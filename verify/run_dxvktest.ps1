# Copied synthetic D3D11 pattern -> verified local DXVK -> Vulkan. No desktop input/activation.
param([string]$CliDir="$PSScriptRoot\..\dist",[Parameter(Mandatory)][string]$Package,
      [string]$Out="$PSScriptRoot\..\build\test\dxvk",[ValidateSet('x64','x86')][string[]]$Architectures=@('x64','x86'),[switch]$PrepareOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\_vcvars.ps1";$vcDir=Get-VcVarsDir
$root=(Resolve-Path "$PSScriptRoot\..").Path;$CliDir=(Resolve-Path $CliDir).Path;$Package=(Resolve-Path $Package).Path
$Out=[IO.Path]::GetFullPath($Out)
if(-not$Out.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)-or$Out.StartsWith($root+'\dist\',[StringComparison]::OrdinalIgnoreCase)){throw 'DXVK evidence must stay inside this worktree and outside dist'}
New-Item -ItemType Directory -Force $Out|Out-Null
$stage=Join-Path $root ('build\dxvk_runtime_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $stage|Out-Null
$provenance=Get-Content (Join-Path $Package 'PROVENANCE.txt') -Raw
if($provenance-notmatch'Official DXVK v3\.1\.1'-or$provenance-notmatch'40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608'){throw 'unexpected local DXVK provenance'}
$provenance|Set-Content (Join-Path $Out 'dependency_provenance.txt')
. "$PSScriptRoot\vi_patterncheck.ps1"
$checks=[Collections.Generic.List[object]]::new();$evidence=[Collections.Generic.List[object]]::new();$hashes=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new()
function Check($value,$name){$checks.Add([pscustomobject]@{pass=[bool]$value;name=$name});Write-Host "$(if($value){'PASS'}else{'FAIL'}) $name";if(-not$value){throw $name}}
function Hash-File($path,$role){$item=Get-Item -LiteralPath $path;$hashes.Add([pscustomobject]@{role=$role;path=$item.FullName;size=$item.Length;sha256=(Get-FileHash -LiteralPath $path).Hash})}
function Remember($process,$path){$owned.Add([pscustomobject]@{process=$process;pid=$process.Id;path=$path;creation=$process.StartTime.ToUniversalTime().Ticks});return $process}
function Start-Owned($exe,[string[]]$arguments,$working,$logs,[switch]$Redirect){
 $si=[Diagnostics.ProcessStartInfo]::new($exe);$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WorkingDirectory=$working
 $si.Environment['DXVK_LOG_PATH']=$logs;$si.Environment['DXVK_LOG_LEVEL']='info';$si.Environment['DXVK_HUD']=''
 foreach($argument in $arguments){$si.ArgumentList.Add($argument)}
 if($Redirect){$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;[void]$p.Start();Remember $p $exe
}
function Wait-Text($path,$pattern,$process,[int]$milliseconds=30000){
 $until=[Environment]::TickCount64+$milliseconds
 do{if(Test-Path -LiteralPath $path){$text=Get-Content -LiteralPath $path -Raw;if($text-match$pattern){return $text}};if($process.HasExited){throw "owned fixture exited $($process.ExitCode) before $pattern"};Start-Sleep -Milliseconds 50}while([Environment]::TickCount64-lt$until)
 throw "owned fixture did not log $pattern within ${milliseconds}ms"
}
function Vi($cli,$targetPid,[string[]]$arguments){$text=&$cli --json --pid $targetPid @arguments;$exit=$LASTEXITCODE;$reply=$text|ConvertFrom-Json;return [pscustomobject]@{exit=$exit;reply=$reply}}
function Stop-OwnedCase {
 foreach($item in $owned){$process=$item.process;if(-not$process.HasExited){$fresh=Get-Process -Id $item.pid -ErrorAction SilentlyContinue;if($fresh-and$fresh.Path-eq$item.path-and$fresh.StartTime.ToUniversalTime().Ticks-eq$item.creation){Stop-Process -Id $item.pid -Force -Confirm:$false}}}
 $owned.Clear()
}
try{
 foreach($architecture in $Architectures){
  $suffix=if($architecture-eq'x64'){''}else{'32'};$pkgArch=if($architecture-eq'x64'){'x64'}else{'x32'};$vcvars=if($architecture-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  $cli=Join-Path $CliDir "vinput$suffix.exe";$build=Join-Path $stage "$architecture\build";New-Item -ItemType Directory -Force $build|Out-Null
  Hash-File $cli 'CLI';Hash-File (Join-Path $CliDir $(if($suffix){'vinput32.dll'}else{'vinput64.dll'})) 'injected DLL'
  $batch=@"
@echo off
setlocal
call "$vcDir\$vcvars" >nul || exit /b 1
pushd "$build"
cl /nologo /std:c++17 /W4 /WX /EHsc /MT /O2 "$root\test\vi_dxvk11.cpp" /Fe:vi_dxvk11.exe /link /SUBSYSTEM:WINDOWS || exit /b 1
exit /b 0
"@
  [IO.File]::WriteAllText((Join-Path $build 'build.cmd'),$batch,[Text.Encoding]::ASCII)
  & cmd /d /c (Join-Path $build 'build.cmd') *> (Join-Path $Out "$architecture.build.log");Check ($LASTEXITCODE-eq0) "$architecture pattern fixture compiles /W4 /WX"
  $buildLog=Get-Content (Join-Path $Out "$architecture.build.log") -Raw;Check ($buildLog-notmatch'(?im)warning [A-Z]+\d+|error [A-Z]+\d+') "$architecture fixture has zero compiler warnings/errors"
  foreach($mode in @('native_d3d11','dxvk_late','dxvk_early')){
   $case="$architecture.$mode";$caseOut=Join-Path $Out $case;$bin=Join-Path $stage "$architecture\$mode";New-Item -ItemType Directory -Force $caseOut,$bin,(Join-Path $caseOut 'dxvk_logs')|Out-Null
   $exe=Join-Path $bin 'vi_dxvk11.exe';Copy-Item (Join-Path $build 'vi_dxvk11.exe') $exe;Hash-File $exe "$case copied synthetic fixture"
   if($mode-ne'native_d3d11'){
    foreach($dll in @('d3d11.dll','dxgi.dll')){$dependency=Join-Path $Package "$pkgArch\$dll";Copy-Item -LiteralPath $dependency -Destination (Join-Path $bin $dll);Hash-File $dependency "$case verified dependency";Check ((Get-FileHash $dependency).Hash-eq(Get-FileHash (Join-Path $bin $dll)).Hash) "$case $dll copy hash matches verified local package"}
   }
   if($PrepareOnly){continue}
   $targetLog=Join-Path $caseOut 'target.log';$logs=Join-Path $caseOut 'dxvk_logs';$target=$null;$launch=$null;$status=$null
   try{
    if($mode-eq'dxvk_early'){
     $lock=Join-Path $caseOut 'private-launch.lock'
     $controller=Start-Owned $cli @('--json','launch','--exe',$exe,'--args',('"'+$targetLog+'"'),'--early','--settle','0','--fg-guard','0','--lock',$lock,'--timeout','30') $bin $logs -Redirect
     $stdout=$controller.StandardOutput.ReadToEndAsync();$stderr=$controller.StandardError.ReadToEndAsync();$until=[Environment]::TickCount64+45000
     do{
      if(-not$target){$child=Get-CimInstance Win32_Process -Filter "ParentProcessId=$($controller.Id)"|Where-Object ExecutablePath -eq $exe|Select-Object -First 1;if($child){$target=Remember (Get-Process -Id $child.ProcessId) $exe}}
      if($controller.HasExited){break};Start-Sleep -Milliseconds 100
     }while([Environment]::TickCount64-lt$until)
     if(-not$controller.HasExited){throw 'owned early launch timed out'}
     $raw=$stdout.GetAwaiter().GetResult();$stderr.GetAwaiter().GetResult()|Set-Content (Join-Path $caseOut 'launch.stderr.txt');$raw|Set-Content (Join-Path $caseOut 'launch.stdout.txt');$launch=$raw|ConvertFrom-Json
     Check ($controller.ExitCode-eq0-and$launch.ok-and$launch.early_injected) "$case suspended early launch succeeds"
     if(-not$target){$target=Remember (Get-Process -Id $launch.pid) $exe}
    }else{
     $target=Start-Owned $exe @($targetLog) $bin $logs
     Wait-Text $targetLog 'DEVICE_READY' $target|Out-Null
     $inject=&$cli --json inject --pid $target.Id|ConvertFrom-Json;Check ($inject.ok) "$case injects only after device creation"
    }
    $ready=Wait-Text $targetLog 'DEVICE_READY' $target
    $status=$null;for($i=0;$i-lt100;$i++){$status=(Vi $cli $target.Id @('status')).reply;if($status.hwnd-ne'0x0'-and$status.frames-gt5){break};Start-Sleep -Milliseconds 50}
    $status|ConvertTo-Json -Depth 14|Set-Content (Join-Path $caseOut 'status_before.json')
    Check ($status.ok-and-not$status.really_foreground-and$status.hwnd-ne'0x0'-and$status.frames-gt5) "$case remains a rendering background process"
    if($mode-ne'native_d3d11'){
     $allLogs=@(Get-ChildItem -LiteralPath $logs -File|ForEach-Object{Get-Content $_.FullName -Raw})-join"`n"
     Check ($allLogs-match'DXVK:\s+v3\.1\.1') "$case actual DXVK log reports v3.1.1"
     Check ($ready.Contains('MODULE d3d11.dll path='+$bin+'\d3d11.dll')-and$ready.Contains('MODULE dxgi.dll path='+$bin+'\dxgi.dll')) "$case loaded the copied DXVK modules rather than system D3D"
     Check ($ready-match'MODULE vulkan-1\.dll path=\S+') "$case actual process loaded vulkan-1.dll"
     Check ($ready-match'VULKAN_PROC_SCOPE pass=1') "$case GIPA preserves unsupported/null-instance scope and forwards a working global enumeration"
    }
    $capture=Vi $cli $target.Id @('capture',(Join-Path $caseOut 'frame1.png'),'--method','native','--timeout-ms','5000')
    $record=[ordered]@{case=$case;pid=$target.Id;launch=$launch;before=$status;capture=$capture}
    $evidence.Add($record)
    if($mode-eq'dxvk_late'){
     Check (-not$capture.reply.ok-or$capture.reply.capture_path-ne'vk') "$case RED: late injection does not prove Vulkan-native capture"
     $record.expected_vk_red=$true
    }else{
     $expected=if($mode-eq'dxvk_early'){'vk'}else{'d3d11'}
     Check ($capture.exit-eq0-and$capture.reply.ok-and$capture.reply.method-eq'swapchain'-and$capture.reply.capture_path-eq$expected) "$case native capture is explicitly $expected"
     $pattern1=Test-VinputPattern (Join-Path $caseOut 'frame1.png');Check ($pattern1.ok) "$case frame1 decodes all bars and frame bits"
     $second=Vi $cli $target.Id @('capture',(Join-Path $caseOut 'frame2.png'),'--method','native','--after-frames','10','--timeout-ms','5000')
     $pattern2=if($second.reply.ok){Test-VinputPattern (Join-Path $caseOut 'frame2.png')}else{$null}
     Check ($second.exit-eq0-and$second.reply.ok-and$second.reply.capture_path-eq$expected-and$pattern2.ok-and$pattern2.frame-ge$pattern1.frame+10-and$second.reply.captured_present-ge$capture.reply.captured_present+10) "$case second native frame and decoded source frame both advance by10"
     $record.pattern1=$pattern1;$record.second=$second;$record.pattern2=$pattern2
    }
    $record.after=(Vi $cli $target.Id @('status')).reply
    if($mode-eq'dxvk_early'){Check ($record.after.frames_by_api.vk-gt0-and$record.after.capture_path-eq'vk') "$case Vulkan frame counters and last capture path agree"}
    if($record.after.log_path){Copy-Item -LiteralPath $record.after.log_path -Destination (Join-Path $caseOut 'vinput.log')}
   }finally{if($status -and $status.log_path -and (Test-Path -LiteralPath $status.log_path)){Copy-Item -LiteralPath $status.log_path -Destination (Join-Path $caseOut 'vinput.log')};Stop-OwnedCase}
  }
 }
}finally{
 Stop-OwnedCase
 $checks|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Out 'checks.json')
 $evidence|ConvertTo-Json -Depth 18|Set-Content (Join-Path $Out 'evidence.json')
 $hashes|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Out 'hashes.json')
}
"$($checks.Count) PASS checks; prepare_only=$([bool]$PrepareOnly)";$global:LASTEXITCODE=0
