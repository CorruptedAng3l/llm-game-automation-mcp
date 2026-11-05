param([string]$Out="$PSScriptRoot\_out\delta_cursor")
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path
$Out=[IO.Path]::GetFullPath($Out);[void](New-Item -ItemType Directory -Force $Out,"$Out\temp")
$env:TEMP="$Out\temp";$env:TMP=$env:TEMP
Push-Location "$root\mcp"
try{
 & npm.cmd run build *> "$Out\build.log";if($LASTEXITCODE){throw 'TypeScript build failed'}
 & node test/t_delta_cursor.mjs *> "$Out\test.log";$code=$LASTEXITCODE
 $text=Get-Content "$Out\test.log" -Raw;Write-Output $text
 if($text-notmatch'delta cursor: (\d+) PASS / (\d+) FAIL'){throw 'missing complete delta cursor result'}
 @{completed=$true;passed=[int]$Matches[1];failed=[int]$Matches[2];exit=$code;scope='finite scoped cursor-response model; no retail cursor or OS input'}|ConvertTo-Json|Set-Content "$Out\result.json"
 exit $code
}finally{Pop-Location}
