param([Parameter(Mandatory=$true)][string]$GameBin,[switch]$ReplaceReShade,[switch]$SelectDBZPreset,[switch]$AcknowledgeEACIncompatibility)
$ErrorActionPreference='Stop'
if(!$AcknowledgeEACIncompatibility){throw 'Known incompatibility: EAC rejected this full-addon ReShade DLL in Xenoverse 2. This installer does not bypass EAC. Do not use for the EAC-protected launch.'}
$root=Split-Path $PSScriptRoot -Parent
$target=(Resolve-Path -LiteralPath $GameBin).Path
if(!(Test-Path -LiteralPath (Join-Path $target 'DBXV2.exe'))){throw 'Expected the Xenoverse 2 bin directory'}
if(Get-Process DBXV2 -ErrorAction SilentlyContinue){throw 'Close Xenoverse 2 before installation'}
$source=Join-Path $root 'dist\local'
$files=@('CelShade.addon64','cel_nvidia.dll','kernels.ptx','CelShade.fx','nvinfer_10.dll','cudart64_12.dll')
foreach($f in $files){if(!(Test-Path -LiteralPath (Join-Path $source $f))){throw "Missing staged file: $f"}}
$backup=Join-Path $root ('build\install-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $backup | Out-Null
$manifest=@()
function Install-File([string]$From,[string]$Relative){
    $to=[IO.Path]::GetFullPath((Join-Path $target $Relative))
    if(!$to.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Install path escapes target'}
    if(Test-Path -LiteralPath $to){
        $save=Join-Path $backup $Relative;New-Item -ItemType Directory -Force (Split-Path $save) | Out-Null
        Copy-Item -LiteralPath $to -Destination $save
    }
    New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
    Copy-Item -LiteralPath $From -Destination $to -Force
    $script:manifest+=@{file=$Relative;sha256=(Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash}
}
if($ReplaceReShade){Install-File (Join-Path $root 'external\reshade-runtime\ReShade64.dll') 'dxgi.dll'}
foreach($f in $files){Install-File (Join-Path $source $f) $f}
Install-File (Join-Path $root 'presets\DBZ.ini') 'CelShade-DBZ.ini'
foreach($f in Get-ChildItem (Join-Path $root 'cache') -File | Where-Object {$_.Name -like '*.plan' -or $_.Name -like '*.plan.ini'}){
    Install-File $f.FullName ('cache\'+$f.Name)
}
$config=Join-Path $target 'ReShade.ini'
if(Test-Path $config){Copy-Item -LiteralPath $config -Destination (Join-Path $backup 'ReShade.ini')}
Add-Type @'
using System.Runtime.InteropServices;
public static class CelIni {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]
 public static extern bool WritePrivateProfileString(string section,string key,string value,string path);
}
'@
$profile=Get-ChildItem (Join-Path $root 'cache') -Filter '*.plan.ini' | Where-Object {
    $text=Get-Content -Raw $_.FullName; $text -match 'Name=whitebox' -and $text -match 'Height=720' -and $text -match 'Precision=fp16'
} | Select-Object -First 1
if(!$profile){throw 'Prepared White-box 720p FP16 profile missing'}
$settings=@{Schema='1';Mode='2';Engine=('cache\'+$profile.Name.Substring(0,$profile.Name.Length-4));Width='1280';Height='720';BGR='1';BypassKey='121'}
foreach($key in $settings.Keys){if(![CelIni]::WritePrivateProfileString('CelShade',$key,$settings[$key],$config)){throw "Config write failed: $key"}}
if($SelectDBZPreset){if(![CelIni]::WritePrivateProfileString('GENERAL','PresetPath','.\CelShade-DBZ.ini',$config)){throw 'Preset config failed'}}
$manifest | ConvertTo-Json | Set-Content (Join-Path $backup 'installed-files.json')
Write-Output "Installed CelShade. Originals and installed-file manifest: $backup"
