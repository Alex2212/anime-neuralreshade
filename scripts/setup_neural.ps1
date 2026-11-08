param([string]$Python='py',[ValidateSet(720,1080)][int]$Height=720)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'bootstrap.ps1') -Python $Python -RuntimeOnly
$width=if($Height -eq 720){1280}else{1920}
Push-Location $root
try {
    New-Item -ItemType Directory -Force build | Out-Null
    foreach($name in @('whitebox','animegan')){
        Write-Output "Exporting and validating $name at ${width}x${Height}..."
        & ./.venv/Scripts/python.exe scripts/prepare_models.py $name --width $width --height $Height --image docs/images/original.png *> "build\$name-export.log"
        if($LASTEXITCODE){Get-Content "build\$name-export.log" -Tail 25;throw 'Model export/reference validation failed'}
        & (Join-Path $PSScriptRoot 'build_engine.ps1') -Model "models\$name-${width}x${Height}.onnx" -Precision fp16
    }
    $target=if((Test-Path CelShade.addon64) -or (Test-Path anime_viewer.exe)){$root}else{Join-Path $root 'dist\local'}
    New-Item -ItemType Directory -Force $target | Out-Null
    Copy-Item external/TensorRT-10.13.3.9/lib/nvinfer_10.dll $target -Force
    Copy-Item external/cuda-runtime/nvidia/cuda_runtime/bin/cudart64_12.dll $target -Force
    if($target -ne $root){
        New-Item -ItemType Directory -Force (Join-Path $target 'cache') | Out-Null
        Copy-Item cache/*.plan,cache/*.plan.ini (Join-Path $target 'cache') -Force
    }
    Write-Output "Neural runtime and both model profiles prepared in $target"
}finally{Pop-Location}
