param([string]$Out="$PSScriptRoot\_out\mouse_message_health")
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp")
$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP
Push-Location "$repo\mcp"
try{& npm run build *> "$Out\build.log";if($LASTEXITCODE){throw 'TypeScript build failed'};& node test/t_mouse_messagehealth.mjs *> "$Out\test.log";$code=$LASTEXITCODE;Get-Content "$Out\test.log";@{completed=$true;exit_code=$code;scope='pure counter diagnostics and fake MCP only; no window/input policy changes tested'}|ConvertTo-Json|Set-Content "$Out\result.json";if($code){exit $code}}finally{Pop-Location}
