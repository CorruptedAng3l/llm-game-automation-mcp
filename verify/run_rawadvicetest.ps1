# Actual doctor field reader/advice branch with finite memory-only status records.
param([string]$Out="$PSScriptRoot/_out/raw_advice",[switch]$OmitAdvice)
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot/..").Path;$Out=[IO.Path]::GetFullPath($Out)
if(-not$Out.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay in checkout'}
New-Item -ItemType Directory -Force $Out,"$Out/temp"|Out-Null;$env:TEMP="$Out/temp";$env:TMP=$env:TEMP
$calls=[Collections.Generic.List[object]]::new();$pins=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$complete=$false;$passed=0;$failed=0
function Pin([string]$path){$pins.Add(@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash})}
# Reuse the exact reviewed gated-process/Job Object prefix, never its test body.
$gateSource="$PSScriptRoot/run_topmosthooktest.ps1";Pin $gateSource;$source=Get-Content -LiteralPath $gateSource -Raw
$first=$source.IndexOf('# Reuse the reviewed fixture-only job implementation');$last=$source.IndexOf('. "$PSScriptRoot\_vcvars.ps1"',$first)
if($first-lt0-or$last-le$first){throw 'reviewed process gate prefix missing'}
$rawAdviceVerifyDir=$PSScriptRoot
. ([scriptblock]::Create($source.Substring($first,$last-$first).Replace('$PSScriptRoot','$rawAdviceVerifyDir')))
. "$PSScriptRoot/_vcvars.ps1";$vcDir=Get-VcVarsDir
$doctor="$repo/src/vi_cli_doctor.inl";Pin $doctor;Pin "$repo/test/vi_rawadvice.cpp";Pin $PSCommandPath;Pin "$PSScriptRoot/_vcvars.ps1"
$text=Get-Content -LiteralPath $doctor -Raw
function Extract([string]$begin,[string]$end){$a=$text.IndexOf($begin);$b=$text.IndexOf($end,$a);if($a-lt0-or$b-le$a){throw "missing extraction $begin"};$text.Substring($a,$b-$a)}
[IO.File]::WriteAllText("$Out/actual_doctor_fields.inl",(Extract 'namespace vi_doctor {' 'static std::string String(')+"`n}")
$advice=Extract '    // RAW_POST_BUFFER_ADVICE_BEGIN' '    // RAW_POST_BUFFER_ADVICE_END';if($OmitAdvice){$advice='// Controlled omission of the actual advice branch only.'}
[IO.File]::WriteAllText("$Out/actual_raw_advice.inl",$advice);Pin "$Out/actual_doctor_fields.inl";Pin "$Out/actual_raw_advice.inl"
try{
 foreach($arch in @('x64','x86')){
  $folder=Join-Path $Out $arch;New-Item -ItemType Directory -Force $folder|Out-Null;$vc=if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'}
  @('@echo off',"call `"$vcDir/$vc`" >nul || exit /b 2", "cl /nologo /O2 /MT /EHsc /W4 /WX /std:c++17 /I`"$Out`" `"$repo/test/vi_rawadvice.cpp`" /Fo:`"$folder/fixture.obj`" /Fe:`"$folder/fixture.exe`" /link /PDB:NONE /INCREMENTAL:NO",'exit /b %errorlevel%')|Set-Content "$folder/build.cmd"
  $build=RunOwned $env:ComSpec @('/d','/c',"$folder/build.cmd") 120000 ($arch+'.build');if($build.exit-ne0){throw "$arch compile failed"};Pin "$folder/fixture.exe"
  $run=RunOwned "$folder/fixture.exe" @() 15000 ($arch+'.test');if($run.stdout-notmatch'raw route advice native: (\d+) PASS / (\d+) FAIL'){throw "$arch missing complete footer"}
  $p=[int]$Matches[1];$f=[int]$Matches[2];$passed+=$p;$failed+=$f;$cases.Add(@{arch=$arch;completed=$true;passed=$p;failed=$f;exit=$run.exit;build=$build;test=$run});Write-Output "$arch $p PASS / $f FAIL"
 }
 $complete=$cases.Count-eq2
}finally{
 $stable=$true;foreach($pin in $pins){if((Get-FileHash -LiteralPath $pin.path).Hash-ne$pin.sha256){$stable=$false}}
 @{completed=$complete;source_stable=$stable;passed=$passed;failed=$failed;cases=$cases;calls=$calls;pins=$pins;omitted_advice=$OmitAdvice.IsPresent;scope='actual extracted doctor parser/advice, finite memory-only records; no target, input, hardware or config mutation; original-handle gated Job Object actors'}|ConvertTo-Json -Depth 12|Set-Content "$Out/result.json"
}
if(-not$complete-or-not$stable-or$failed){exit 1};exit 0
