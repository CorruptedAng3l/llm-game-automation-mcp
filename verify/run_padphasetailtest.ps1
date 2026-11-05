# Memory streams only; no processes, pipe servers, GUI, GPU or input.
param([string]$Out="$PSScriptRoot\_out\padphase_tail")
$ErrorActionPreference='Stop';New-Item -ItemType Directory -Force $Out|Out-Null
Add-Type -Path "$PSScriptRoot\..\test\vi_padphase_tail.cs";$checks=@()
foreach($size in @(0,65537,1048576,1048577)){
 $text='x'*$size;$stream=[IO.MemoryStream]::new([Text.Encoding]::UTF8.GetBytes($text));$reader=[IO.StreamReader]::new($stream);$tail=[ViPadPhaseTail]::new($reader)
 try{$done=$tail.Wait(2000);$good=if($size-le1048576){$done-and$tail.Complete-and-not$tail.Error-and$tail.Text()-ceq$text-and$tail.Characters-eq$size}else{$done-and$tail.Complete-and$tail.Error-and$tail.Text().Length-le1048576-and$tail.Characters-eq$size};$checks+=@{pass=[bool]$good;input_characters=$size};Write-Host "$(if($good){'PASS'}else{'FAIL'}) bounded tail $size characters"}finally{$reader.Dispose();$stream.Dispose()}
}
$failed=@($checks|Where-Object pass -eq $false).Count;@{completed=$true;passed=$checks.Count-$failed;failed=$failed;checks=$checks;scope='memory-only output limit and full >64KiB journal preservation'}|ConvertTo-Json|Set-Content "$Out\result.json";if($failed){exit 1}
