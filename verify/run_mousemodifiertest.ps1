param([string]$Out="$PSScriptRoot\_out\mouse_modifiers")
$ErrorActionPreference='Stop';$repo=(Resolve-Path "$PSScriptRoot\..").Path
[void](New-Item -ItemType Directory -Force $Out,"$repo\_scratch\temp");$env:TEMP="$repo\_scratch\temp";$env:TMP=$env:TEMP
Push-Location "$repo\mcp"
try{& npm run build *> "$Out\build.log";if($LASTEXITCODE){throw 'TypeScript build failed'};& node test/t_mouse_modifiers.mjs *> "$Out\test.log";$code=$LASTEXITCODE;Get-Content "$Out\test.log";@{completed=$true;exit_code=$code;scope='bounded native job protocol model and fake MCP; no OS input or actual native input job execution'}|ConvertTo-Json|Set-Content "$Out\result.json";if($code){exit $code}}finally{Pop-Location}
