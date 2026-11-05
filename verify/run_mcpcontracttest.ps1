param([Parameter(Mandatory)][ValidatePattern('^[a-z0-9_]+$')][string]$Test,[string]$Out="$PSScriptRoot\_out\mcp_contract")
$ErrorActionPreference='Stop';$root=(Resolve-Path "$PSScriptRoot\..").Path
$Out=[IO.Path]::GetFullPath($Out);[void](New-Item -ItemType Directory -Force $Out,"$Out\temp")
$env:TEMP="$Out\temp";$env:TMP=$env:TEMP
Push-Location "$root\mcp"
try{
 & npm.cmd run build *> "$Out\build.log";if($LASTEXITCODE){throw 'TypeScript build failed'}
 & node "test/t_$Test.mjs" *> "$Out\test.log";$code=$LASTEXITCODE
 $text=Get-Content "$Out\test.log" -Raw;Write-Output $text
 if($text-notmatch'(\d+) PASS / (\d+) FAIL\s*$'){throw 'missing complete MCP contract result'}
 @{completed=$true;test=$Test;passed=[int]$Matches[1];failed=[int]$Matches[2];exit=$code;scope='MCP contract with simulated CLI; no retail gameplay'}|ConvertTo-Json|Set-Content "$Out\result.json"
 exit $code
}finally{Pop-Location}
