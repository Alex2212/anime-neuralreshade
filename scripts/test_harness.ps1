$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dir=Join-Path $root 'build\harness'
New-Item -ItemType Directory -Force $dir | Out-Null
foreach($name in @('CelShade.addon64','cel_nvidia.dll','kernels.ptx','cudart64_12.dll','nvinfer_10.dll','cel_test_host.exe','CelShade.fx')){
    Copy-Item -LiteralPath (Join-Path $root "dist\local\$name") -Destination $dir -Force
}
Copy-Item -LiteralPath (Join-Path $root 'external\reshade-runtime\ReShade64.dll') -Destination (Join-Path $dir 'dxgi.dll') -Force
@'
[GENERAL]
EffectSearchPaths=.\
TextureSearchPaths=.\
PresetPath=.\Identity.ini
NoReloadOnInit=0
NoEffectCache=1
[CelShade]
Mode=1
Width=640
Height=360
BGR=0
'@ | Set-Content (Join-Path $dir 'ReShade.ini')
@'
Techniques=Cel_Neural@CelShade.fx
[CelShade.fx]
CelStrength=1.0
'@ | Set-Content (Join-Path $dir 'Identity.ini')
Push-Location $dir
try {
    $run=Start-Process -FilePath .\cel_test_host.exe -ArgumentList '--verify-identity' -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput harness-result.txt -RedirectStandardError harness-errors.txt
    Get-Content harness-result.txt,harness-errors.txt
    if($run.ExitCode -ne 0){throw "GPU / ReShade identity harness failed: $($run.ExitCode)"}
    if(!(Test-Path harness-counts.txt)){throw 'Harness did not write its completion evidence'}
    $log=Get-Content -Raw ReShade.log
    if($log -notmatch 'Registered add-on "CelShade"' -or $log -notmatch 'Successfully compiled' -or $log -match '\| ERROR \|'){throw 'Addon load or FX compilation failed; inspect ReShade.log'}
}finally{Pop-Location}
