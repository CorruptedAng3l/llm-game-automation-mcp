# Inert parser/verdict contracts; no window, target process or desktop input.
param([string]$Out="$PSScriptRoot\..\build\test\_out\recenteridentity")
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $Out|Out-Null;$Out=(Resolve-Path $Out).Path
. "$PSScriptRoot\vi_textlog.ps1"
$ast=[Management.Automation.Language.Parser]::ParseFile("$PSScriptRoot\run_recenterguardtest.ps1",[ref]$null,[ref]$null)
foreach($name in @('ReadOwnedStart','SaveResult','Check')){
 $definition=$ast.Find({param($node)$node-is[Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-eq$name},$true)
 if(-not$definition){throw "missing $name"};. ([scriptblock]::Create($definition.Extent.Text))
}
$contracts=[Collections.Generic.List[object]]::new()
function Assert($Ok,$Name){$contracts.Add(@{pass=[bool]$Ok;name=$Name});if(-not$Ok){throw $Name};"PASS $Name"}
$old='START pid=\d+ hwnd=(?:0x)?([0-9A-Fa-f]+)'
$partial='10 START pid=123 hwnd=0000'
$stale="10 START pid=456 hwnd=0000ABCD noactivate=1`n11 RECENTER_GUARD_VERSION 1 guarded=1`n"
Assert ($partial-match$old) 'RED old parser accepts a partially written HWND'
Assert ($stale-match$old) 'RED old parser accepts a previous worker PID'
Assert ((ReadOwnedStart $partial 123)-eq[IntPtr]::Zero) 'GREEN incomplete line refused'
Assert ((ReadOwnedStart $stale 123)-eq[IntPtr]::Zero) 'GREEN stale PID refused'
Assert ((ReadOwnedStart "10 START pid=123 hwnd=0000ABCD noactivate=1`n" 123).ToInt64()-eq0xABCD) 'GREEN complete current worker line accepted'
Assert ((ReadOwnedStart "10 START pid=123 hwnd=0000ABCD noactivate=1`r`n" 123).ToInt64()-eq0xABCD) 'GREEN CRLF accepted'
$checks=[Collections.Generic.List[object]]::new();$records=[Collections.Generic.List[object]]::new()
$records.Add(@{ownership=@(@{hwnd='0xABCD';worker_pid=123;owner_pid=456;is_window=$true})})
$caught=$false;try{$null=Check $false 'inert intentional failed ownership'}catch{$caught=$true}
$result=Get-Content "$Out\result.json" -Raw|ConvertFrom-Json
Assert ($caught-and$result.checks.Count-eq1-and-not$result.checks[0].pass) 'GREEN failed check persisted before throw'
Assert ($result.evidence[0].ownership[0].owner_pid-eq456) 'GREEN ownership diagnostics survive throw'
$pieces=@("prefix`n","321 NEIGHBOR_BEGIN duration_ms=6000`n");$Matches=$null
Assert ([bool]($pieces-match'(\d+) NEIGHBOR_BEGIN')-and$null-eq$Matches) 'RED collection match succeeds without publishing scalar Matches'
$live=Join-Path $Out 'shared_live.log';$stream=[IO.FileStream]::new($live,[IO.FileMode]::Create,[IO.FileAccess]::Write,([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));$writer=[IO.StreamWriter]::new($stream)
try{
 $writer.Write('321 NEIGHBOR_BEGIN ');$writer.Flush();$snapshot=Read-ViSharedText $live
 Assert ($snapshot-is[string]-and-not[regex]::Match($snapshot,'(\d+) NEIGHBOR_BEGIN duration_ms=6000').Success) 'GREEN partial shared snapshot remains scalar and incomplete'
 $writer.Write("duration_ms=6000`n");$writer.Flush();$snapshot=Read-ViSharedText $live;$begin=[regex]::Match($snapshot,'(\d+) NEIGHBOR_BEGIN duration_ms=6000')
 Assert ($snapshot-is[string]-and$begin.Success-and$begin.Groups[1].Value-eq'321') 'GREEN completed shared writer snapshot has stable explicit regex groups'
}finally{$writer.Dispose();$stream.Dispose()}
$contracts|ConvertTo-Json -Depth 8|Set-Content "$Out\contracts.json"
"$($contracts.Count) PASS / 0 FAIL";$global:LASTEXITCODE=0
