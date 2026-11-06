param([string]$Out="$PSScriptRoot\_out\public_safeguards")
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent));$output=[IO.Path]::GetFullPath($Out)
. "$PSScriptRoot/_vcvars.ps1";$vcDir=Get-VcVarsDir
if(-not$output.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside checkout'}
[void](New-Item -ItemType Directory -Force -Path @($output,"$output/temp"));$env:TEMP="$output/temp";$env:TMP=$env:TEMP;$cases=@();$pins=@()
foreach($arch in @('x64','x86')){
 $folder=Join-Path $output $arch;[void](New-Item -ItemType Directory -Force $folder);$vc=Join-Path $vcDir $(if($arch-eq'x64'){'vcvars64.bat'}else{'vcvars32.bat'})
 foreach($name in @('vimh_buffer.obj','vimh_hook.obj','vimh_trampoline.obj','vimh_hde.obj')){$origin=Join-Path "$root/build/$arch" $name;$copy=Join-Path $folder $name;Copy-Item -LiteralPath $origin -Destination $copy;$hash=(Get-FileHash -LiteralPath $origin).Hash;if((Get-FileHash -LiteralPath $copy).Hash-ne$hash){throw 'isolated vimh object copy mismatch'};$pins+=@{arch=$arch;source=$origin;copy=$copy;sha256=$hash}}
 $command=@"
@echo off
call "$vc" >nul || exit /b 2
pushd "$folder" || exit /b 2
cl /nologo /O2 /MT /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /std:c++17 /FI"$root\src\vi_mh_rename.h" /I"$root\src" /I"$root\third_party\minhook\include" "$root\test\vi_public_safeguards.cpp" vimh_buffer.obj vimh_hook.obj vimh_trampoline.obj vimh_hde.obj /Fo:"$folder\fixture.obj" /Fe:"$folder\fixture.exe" /link /PDB:NONE /INCREMENTAL:NO || exit /b 2
"$folder\fixture.exe"
"@
 $command|Set-Content -LiteralPath "$folder/run.cmd" -Encoding ascii
 & cmd /d /c "$folder\run.cmd" *> "$folder/result.log";$code=$LASTEXITCODE;$text=[IO.File]::ReadAllText("$folder/result.log");$parsed=$text-match'public safeguards: (\d+) PASS / (\d+) FAIL';$passed=0;$failed=1;if($parsed){$passed=[int]$Matches[1];$failed=[int]$Matches[2]}
 $cases+=@{arch=$arch;exit=$code;completed=$parsed;passed=$passed;failed=$failed;log="$folder/result.log"};Write-Output "$arch $passed PASS / $failed FAIL"
}
@{completed=(@($cases|Where-Object completed -eq $true).Count-eq2);cases=$cases;object_pins=$pins;scope='actual production memory-only callbacks with terminal writers; no Init, GUI, device, worker or game'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$output/result.json" -Encoding utf8NoBOM
if(@($cases|Where-Object {$_.exit-ne0-or$_.failed-ne0}).Count){exit 1}
