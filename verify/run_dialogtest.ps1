# A fatal Unity-like #32770 must not be reported as a healthy launched game. Synthetic only, no foreground.
param([string]$CliDir="$PSScriptRoot\..\dist",[string]$TargetDir="$PSScriptRoot\..\dist",[string]$Out="$PSScriptRoot\..\build\test\_out\dialog")
$ErrorActionPreference='Stop';$V=(Resolve-Path "$CliDir\vinput.exe").Path;$D=(Resolve-Path $TargetDir).Path
New-Item -ItemType Directory -Force $Out | Out-Null;$Out=(Resolve-Path $Out).Path
$checks=@();$evidence=@()
if(-not ('ViDialogNative' -as [type])) { Add-Type 'using System;using System.Text;using System.Runtime.InteropServices;public static class ViDialogNative{[DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,StringBuilder text,int size);}' }
function Check($ok,$name){$script:checks += [pscustomobject]@{pass=[bool]$ok;name=$name};"$(if($ok){'PASS'}else{'FAIL'}) $name"}
foreach($exe in @('vi_focusprobe.exe','vi_focusprobe32.exe')){
 foreach($mode in @('normal','dialog','dialog_allowed','dialog_noinject','dialog_early')){
  $owned=$null
  try{
   $targetLog="$Out\$exe.$mode.log"
   $targetArgs="`"$targetLog`"";if($mode -ne 'normal'){$targetArgs+=' --dialog'}
   $a=@('--json','launch','--exe',"$D\$exe",'--args',$targetArgs,'--settle','0','--fg-guard','0','--timeout','8')
   if($mode -eq 'dialog_allowed'){$a+='--allow-dialog'}
   if($mode -eq 'dialog_noinject'){$a+='--no-inject'}
   if($mode -eq 'dialog_early'){$a+='--early'}
   $lines=& $V @a 2> "$Out\$exe.$mode.stderr";$exit=$LASTEXITCODE
   $r=$lines|ConvertFrom-Json;if($r.pid){$owned=Get-Process -Id $r.pid -ErrorAction SilentlyContinue}
   $windowLine=Get-Content -LiteralPath $targetLog | Where-Object {$_ -match ' WINDOW hwnd=0x[0-9A-F]+'} | Select-Object -First 1
   $window=[IntPtr]::Zero
   if($windowLine -match 'hwnd=0x([0-9A-F]+)'){$window=[IntPtr][long]::Parse($Matches[1],[Globalization.NumberStyles]::HexNumber)}
   $actualClass=[Text.StringBuilder]::new(128);[ViDialogNative]::GetClassName($window,$actualClass,128)|Out-Null
   Check ($actualClass.ToString() -eq $(if($mode -eq 'normal'){'vi_focusprobe'}else{'#32770'})) "$exe $mode actual fixture class verified independently"
   $reject=$mode -in @('dialog','dialog_noinject','dialog_early')
   if($reject){
    Check (-not $r.ok -and $exit -ne 0 -and $r.window_class -eq '#32770' -and $r.error -match 'dialog') "$exe $mode rejected with class and error"
    Check ($r.dialog_text -match 'Forced GfxDevice.*Vulkan' -and $r.window_title -eq 'Fatal Error') "$exe $mode includes actual dialog message"
   }else{
    Check ($r.ok -and $exit -eq 0) "$exe $mode launches"
    Check ($r.window_class -eq $(if($mode -eq 'normal'){'vi_focusprobe'}else{'#32770'})) "$exe $mode reports main window class"
   }
   $evidence += [pscustomobject]@{target=$exe;mode=$mode;target_args=$targetArgs;actual_class=$actualClass.ToString();exit=$exit;reply=$r}
  }finally{if($owned -and -not $owned.HasExited){Stop-Process -Id $owned.Id -Force -Confirm:$false}}
 }
}
$checks|ConvertTo-Json -Depth 4|Set-Content "$Out\checks.json";$evidence|ConvertTo-Json -Depth 6|Set-Content "$Out\evidence.json"
$failed=@($checks|Where-Object {-not $_.pass}).Count;"$($checks.Count-$failed) PASS / $failed FAIL";if($failed){exit 1}
$global:LASTEXITCODE=0 # expected rejected-dialog native exits must not leak into the calling suite
