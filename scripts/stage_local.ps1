$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$stage=Join-Path $root 'dist\local'
New-Item -ItemType Directory -Force $stage | Out-Null
foreach($name in @('CelShade.addon64','cel_nvidia.dll','cel_benchmark.exe','cel_test_host.exe','anime_viewer.exe')){Copy-Item -LiteralPath (Join-Path $root "build\$name") -Destination $stage -Force}
Copy-Item -LiteralPath (Join-Path $root 'external\TensorRT-10.13.3.9\lib\nvinfer_10.dll') -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $root 'external\cuda-runtime\nvidia\cuda_runtime\bin\cudart64_12.dll') -Destination $stage -Force
Copy-Item -Path (Join-Path $root 'shaders\*') -Destination $stage -Force
Copy-Item -Path (Join-Path $root 'presets\*') -Destination $stage -Force
$env:PATH=(Join-Path $root 'external\cuda-nvrtc\nvidia\cuda_nvrtc\bin')+';'+$env:PATH
& (Join-Path $root 'build\cel_compile_kernels.exe') (Join-Path $root 'src\inference\kernels.cu') (Join-Path $stage 'kernels.ptx') (Join-Path $root 'external\cuda-runtime\nvidia\cuda_runtime\include')
if($LASTEXITCODE -ne 0){throw 'Kernel compilation failed'}
Get-ChildItem -LiteralPath $stage -File | Get-FileHash -Algorithm SHA256 | Select-Object @{n='file';e={Split-Path $_.Path -Leaf}},Hash | ConvertTo-Json | Set-Content (Join-Path $stage 'checksums.json')
Write-Output "Private local test package: $stage"
