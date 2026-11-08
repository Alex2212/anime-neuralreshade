param([Parameter(Mandatory=$true)][string]$Model,[ValidateSet('fp32','fp16')][string]$Precision='fp32')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$modelPath=(Resolve-Path -LiteralPath $Model).Path
$modelHash=(Get-FileHash -Algorithm SHA256 -LiteralPath $modelPath).Hash.ToLowerInvariant()
$gpu=(& nvidia-smi --query-gpu=name,uuid,compute_cap,driver_version --format=csv,noheader | Select-Object -First 1).Trim()
$flags=@('--skipInference','--noTF32','--builderOptimizationLevel=3')
if($Precision -eq 'fp16'){$flags+='--fp16'}
$identity="schema=1;model=$modelHash;runtime=10.13.3.9;gpu=$gpu;precision=$Precision;flags=$($flags -join ',')"
$sha=[Security.Cryptography.SHA256]::Create()
$key=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($identity)))).Replace('-','').ToLowerInvariant()
$cache=Join-Path $root 'cache';New-Item -ItemType Directory -Force $cache | Out-Null
$engine=Join-Path $cache "$key.plan"
$meta="$engine.json"
$contract=Get-Content -Raw -LiteralPath ([IO.Path]::ChangeExtension($modelPath,'.json')) | ConvertFrom-Json
function Write-EngineContract {
    $engineHash=(Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()
    $uuid=($gpu -split ',')[1].Trim()
    @('[Engine]','Schema=1',"Name=$($contract.name)","SHA256=$engineHash", "ModelSHA256=$modelHash",'TensorRT=10.13.3.9',"GPU=$uuid","Width=$($contract.shape[2])","Height=$($contract.shape[1])","Color=$($contract.color)","Precision=$Precision") | Set-Content -LiteralPath "$engine.ini" -Encoding ASCII
}
if((Test-Path -LiteralPath $engine) -and (Test-Path -LiteralPath $meta)) {
    $cached=Get-Content -Raw -LiteralPath $meta | ConvertFrom-Json
    if($cached.identity -eq $identity -and $cached.engine_sha256 -eq (Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()){Write-EngineContract;Write-Output $engine;exit 0}
}
$sdk=Join-Path $root 'external\TensorRT-10.13.3.9'
$env:PATH=(Join-Path $sdk 'lib')+';'+(Join-Path $root 'external\cuda-runtime\nvidia\cuda_runtime\bin')+';'+$env:PATH
$arguments=@(('"--onnx='+$modelPath+'"'),('"--saveEngine='+$engine+'"'))+$flags
$process=Start-Process -FilePath (Join-Path $sdk 'bin\trtexec.exe') -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $cache "$key.build.log") -RedirectStandardError (Join-Path $cache "$key.build.stderr.log")
if($process.ExitCode -ne 0){Get-Content (Join-Path $cache "$key.build.log"),(Join-Path $cache "$key.build.stderr.log") -Tail 40;throw 'TensorRT engine build failed'}
@{schema=1;identity=$identity;model_sha256=$modelHash;gpu=$gpu;precision=$Precision;runtime='10.13.3.9';flags=$flags;engine_sha256=(Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $meta
Write-EngineContract
Write-Output $engine
