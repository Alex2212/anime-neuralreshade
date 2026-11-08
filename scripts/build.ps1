param([switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC x64 build tools required' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$env:PATH=(Split-Path $vswhere)+';'+$env:PATH
New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
$cmdFile = Join-Path $root 'build\build-local.cmd'
$commands = @('@echo off', ('call "' + $vcvars + '" >nul'), ('cd /d "' + $root + '"'), 'cmake --preset windows-release', 'if errorlevel 1 exit /b %errorlevel%', 'cmake --build --preset windows-release', 'if errorlevel 1 exit /b %errorlevel%')
if ($Test) { $commands += 'ctest --preset windows-release' }
[IO.File]::WriteAllLines($cmdFile, $commands)
& $cmdFile
if ($LASTEXITCODE -ne 0) { throw "Build/check failed: $LASTEXITCODE" }
