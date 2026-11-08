param([string]$Version='0.1.0-preview.4',[string]$RuntimeStage='dist/local')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$ptx=Join-Path (Join-Path $root $RuntimeStage) 'kernels.ptx'
if((Get-Content -LiteralPath $ptx -Raw) -notmatch '\.entry\s+pack_model\('){throw 'Compile current kernels.cu into RuntimeStage first; native model-grid export is missing'}
$package=Join-Path $root "dist\anime-neuralreshade-$Version-windows-x64"
if(Test-Path $package){throw 'Use a fresh version/package directory; refusing to mix old release contents'}
New-Item -ItemType Directory -Force $package | Out-Null
foreach($name in @('CelShade.addon64','cel_nvidia.dll','kernels.ptx','CelShade.fx','DBZ.ini','anime_viewer.exe')){
    $source=if($name -match '\.(exe|dll|addon64)$'){Join-Path $root "build\$name"}else{Join-Path (Join-Path $root $RuntimeStage) $name}
    Copy-Item -LiteralPath $source -Destination $package
}
foreach($name in @('README.md','BUILDING.md','BENCHMARKS.md','COMPATIBILITY.md','THIRD_PARTY_NOTICES.md','DEVELOPMENT_STATUS.md')){Copy-Item -LiteralPath (Join-Path $root $name) -Destination $package}
New-Item -ItemType Directory -Force (Join-Path $package 'scripts'),(Join-Path $package 'notices'),(Join-Path $package 'docs\images') | Out-Null
foreach($name in @('bootstrap.ps1','setup_neural.ps1','prepare_models.py','build_engine.ps1','requirements-offline.lock.txt','test_viewer.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $package 'scripts')}
Copy-Item -Path (Join-Path $root 'notices\*') -Destination (Join-Path $package 'notices')
Copy-Item -Path (Join-Path $root 'docs\images\*') -Destination (Join-Path $package 'docs\images')
$hashes=Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName | ForEach-Object{
    $relative=$_.FullName.Substring($package.Length+1).Replace('\','/')
    ((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())+'  '+$relative
}
$hashes | Set-Content (Join-Path $package 'SHA256SUMS.txt') -Encoding ASCII
$zip="$package.zip"
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash -LiteralPath $zip -Algorithm SHA256 | ForEach-Object {$_.Hash.ToLowerInvariant()+'  '+(Split-Path $_.Path -Leaf)} | Set-Content "$zip.sha256" -Encoding ASCII
Write-Output $zip
