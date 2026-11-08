param([string]$Python='py',[switch]$RuntimeOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Force external/downloads | Out-Null
    function Fetch([string]$Url,[string]$File,[string]$Hash) {
        $path=Join-Path $root "external\downloads\$File"
        if(!(Test-Path -LiteralPath $path)) { & curl.exe -L --fail --silent --show-error $Url -o $path; if($LASTEXITCODE -ne 0){throw "Download failed: $File"} }
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Hash){throw "SHA256 mismatch: $File; remove the incomplete download and retry"}
        return $path
    }
    function Expand-Checked([string]$Archive,[string]$RelativeDestination) {
        $destination=[IO.Path]::GetFullPath((Join-Path $root $RelativeDestination))
        $marker=Join-Path $destination '.extracted'
        if(Test-Path -LiteralPath $marker){return}
        New-Item -ItemType Directory -Force $destination | Out-Null
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
        try {foreach($entry in $zip.Entries){
            $target=[IO.Path]::GetFullPath((Join-Path $destination $entry.FullName))
            if(!$target.StartsWith($destination+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe archive path'}
            if(!$entry.Name){New-Item -ItemType Directory -Force $target | Out-Null;continue}
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$true)
        }}finally{$zip.Dispose()}
        Set-Content -LiteralPath $marker -Value 'SHA256-verified archive extracted'
    }
    function Checkout([string]$Name,[string]$Url,[string]$Revision) {
        $path=Join-Path $root "external\$Name"
        if(!(Test-Path (Join-Path $path '.git'))){
            & git init $path; if($LASTEXITCODE){throw 'git init failed'}
            & git -C $path remote add origin $Url
            & git -C $path fetch --depth 1 origin $Revision; if($LASTEXITCODE){throw "Fetch failed: $Name"}
            & git -C $path checkout --detach FETCH_HEAD; if($LASTEXITCODE){throw "Checkout failed: $Name"}
        }
        if((& git -C $path rev-parse HEAD) -ne $Revision){throw "Unexpected revision in $Name; preserve local edits and use the pinned dependency"}
    }
    if(!(Test-Path .venv/Scripts/python.exe)){
        if($Python -eq 'py'){& py -3.11 -m venv .venv}else{& $Python -m venv .venv}
        if($LASTEXITCODE){throw 'Python 3.11 required; pass -Python with its executable path'}
    }
    & ./.venv/Scripts/python.exe -m pip install -r scripts/requirements-offline.lock.txt
    if($LASTEXITCODE){throw 'Offline conversion dependencies failed'}
    $trt=Fetch 'https://developer.download.nvidia.com/compute/machine-learning/tensorrt/10.13.3/zip/TensorRT-10.13.3.9.Windows.win10.cuda-12.9.zip' 'TensorRT-10.13.3.9.zip' 'D7FD63DC111DC68BC7BDF3B0B494C669ED50315502B016EC3022AC7CB3BA0615'
    # This archive has its own versioned top-level folder.
    if(!(Test-Path external/TensorRT-10.13.3.9/lib/nvinfer_10.dll)){Expand-Checked $trt 'external'}
    $packages=,@('nvidia-cuda-runtime-cu12','12.9.79','nvidia_cuda_runtime_cu12-12.9.79-py3-none-win_amd64.whl','8E018AF8FA02363876860388BD10CCB89EB9AB8FB0AA749AAF58430A9F7C4891','cuda-runtime')
    if(!$RuntimeOnly){$packages+=@(
        @('nvidia-cuda-nvrtc-cu12','12.9.86','nvidia_cuda_nvrtc_cu12-12.9.86-py3-none-win_amd64.whl','72972EBDCF504D69462D3BCD67E7B81EDD25D0FB85A2C46D3EA3517666636349','cuda-nvrtc'),
        @('nvidia-cuda-nvcc-cu12','12.9.86','nvidia_cuda_nvcc_cu12-12.9.86-py3-none-win_amd64.whl','8ED7F0B17DEA662755395BE029376DB3B94FED5CBB17C2D35CC866C5B1B84099','cuda-nvcc')
    )}
    foreach($p in $packages){
        $metadata=Invoke-RestMethod "https://pypi.org/pypi/$($p[0])/$($p[1])/json"
        $url=($metadata.urls | Where-Object filename -EQ $p[2] | Select-Object -First 1).url
        if(!$url){throw "Pinned wheel unavailable: $($p[2])"}
        $archive=Fetch $url $p[2] $p[3];Expand-Checked $archive "external\$($p[4])"
    }
    Checkout 'AnimeGANv2' 'https://github.com/TachibanaYoshino/AnimeGANv2.git' '724b5ce75185c2e838d30c9077dbb967ab034254'
    Checkout 'White-box-Cartoonization' 'https://github.com/SystemErrorWang/White-box-Cartoonization.git' 'ee97277d7fd873b89e9b8e1341efe74e10dd4124'
    if(!$RuntimeOnly){
        Checkout 'reshade' 'https://github.com/crosire/reshade.git' 'a59d611903db84355d562a2cb6da20d4d2761619'
        & git -C external/reshade submodule update --init --depth 1 deps/imgui
        if($LASTEXITCODE){throw 'Matching ImGui headers unavailable'}
        Copy-Item -Path external/cuda-nvcc/nvidia/cuda_nvcc/include/* -Destination external/cuda-runtime/nvidia/cuda_runtime/include -Recurse -Force
        $reshade=Fetch 'https://reshade.me/downloads/ReShade_Setup_6.1.1_Addon.exe' 'ReShade_Setup_6.1.1_Addon.exe' '928AA45E2588AFC18D99920FA9D53388242D6E59A0F796C3EA001E6B5061CA06'
        & ./.venv/Scripts/python.exe -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall('external/reshade-runtime')" $reshade
        if($LASTEXITCODE){throw 'ReShade test runtime extraction failed'}
    }
    Write-Output 'Pinned local dependencies prepared. No game files changed.'
}finally{Pop-Location}
