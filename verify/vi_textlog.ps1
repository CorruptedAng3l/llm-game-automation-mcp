# A live file can grow during Get-Content -Raw and yield multiple pipeline strings.
# Read one shared snapshot; do not use the ambient $Matches variable for identity.
function Read-ViSharedText([string]$Path){
 if(-not[IO.File]::Exists($Path)){return ''}
 $stream=[IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete))
 try{$reader=[IO.StreamReader]::new($stream);try{return $reader.ReadToEnd()}finally{$reader.Dispose()}}finally{$stream.Dispose()}
}
