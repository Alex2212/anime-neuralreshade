param([string]$Python='py',[ValidateSet(720,1080,1440)][int]$Height=720,[ValidateSet('both','whitebox','animegan')][string]$Model='both')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'bootstrap.ps1') -Python $Python -RuntimeOnly
$width=switch($Height){720{1280}1080{1920}1440{2560}}
$modelNames=if($Model -eq 'both'){@('whitebox','animegan')}else{@($Model)}
Push-Location $root
try {
    New-Item -ItemType Directory -Force build | Out-Null
    foreach($name in $modelNames){
        Write-Output "Exporting and validating $name at ${width}x${Height}..."
        # Windows PowerShell can promote native stderr warnings into terminating errors.
        # Keep TensorFlow diagnostics separate and use the actual process exit status.
        $export=Start-Process -FilePath (Join-Path $root '.venv\Scripts\python.exe') -ArgumentList @('scripts/prepare_models.py',$name,'--width',$width,'--height',$Height,'--image','docs/images/original.png') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput "build\$name-export.log" -RedirectStandardError "build\$name-export.stderr.log"
        if($export.ExitCode -ne 0){Get-Content "build\$name-export.log","build\$name-export.stderr.log" -Tail 25;throw 'Model export/reference validation failed'}
        & (Join-Path $PSScriptRoot 'build_engine.ps1') -Model "models\$name-${width}x${Height}.onnx" -Precision fp16
    }
    $target=if((Test-Path CelShade.addon64) -or (Test-Path anime_viewer.exe)){$root}else{Join-Path $root 'dist\local'}
    New-Item -ItemType Directory -Force $target | Out-Null
    foreach($source in @('external/TensorRT-10.13.3.9/lib/nvinfer_10.dll','external/cuda-runtime/nvidia/cuda_runtime/bin/cudart64_12.dll')){
        $destination=Join-Path $target (Split-Path $source -Leaf)
        if((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $destination).Hash){continue}
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    if($target -ne $root){
        New-Item -ItemType Directory -Force (Join-Path $target 'cache') | Out-Null
        Copy-Item cache/*.plan,cache/*.plan.ini (Join-Path $target 'cache') -Force
    }
    Write-Output "Neural runtime and $($modelNames -join ', ') profiles at ${width}x${Height} prepared in $target"
}finally{Pop-Location}
