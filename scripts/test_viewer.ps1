param([string]$Viewer='', [string]$Engine='', [string]$Report='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$Viewer){$Viewer=if(Test-Path (Join-Path $root 'anime_viewer.exe')){Join-Path $root 'anime_viewer.exe'}else{Join-Path $root 'dist\local\anime_viewer.exe'}}
if(!$Report){$Report=Join-Path $root 'build\viewer-self-test.json'}
$Viewer=[IO.Path]::GetFullPath($Viewer)
$Report=[IO.Path]::GetFullPath($Report)
New-Item -ItemType Directory -Force (Split-Path $Report -Parent) | Out-Null
if($Viewer.Contains('"') -or $Report.Contains('"') -or $Engine.Contains('"')){throw 'Quotes are not valid in paths'}
$arguments='--self-test --report "'+$Report+'"'
if($Engine){$arguments+=' --engine "'+[IO.Path]::GetFullPath($Engine)+'"'}
# Self-test intentionally creates its own animated source and output windows.
$process=Start-Process -FilePath $Viewer -ArgumentList $arguments -PassThru -WindowStyle Hidden
if(!$process.WaitForExit(45000)){Stop-Process -Id $process.Id;throw 'Viewer test exceeded 45 seconds'}
$result=Get-Content -LiteralPath $Report -Raw | ConvertFrom-Json
if($process.ExitCode -ne 0 -or !$result.passed){
    if(Test-Path -LiteralPath "$Report.error.txt"){Get-Content -LiteralPath "$Report.error.txt"}
    throw "Viewer self-test failed: exit $($process.ExitCode)"
}
$result | ConvertTo-Json
