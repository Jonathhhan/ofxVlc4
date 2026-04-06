param(
    [string]$Preset = "oceanside-4k",
    [string]$OutputDir = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message"
}

function Resolve-AddonRoot {
    param([string]$StartDir)

    $current = Resolve-Path $StartDir
    while ($null -ne $current) {
        $libsDir = Join-Path $current.Path "libs"
        $configFile = Join-Path $current.Path "addon_config.mk"
        if ((Test-Path $libsDir) -and (Test-Path $configFile)) {
            return $current.Path
        }

        $parent = Split-Path $current.Path -Parent
        if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $current.Path) {
            break
        }
        $current = Resolve-Path $parent
    }

    throw "Could not resolve addon root from '$StartDir'."
}

$addonRoot = Resolve-AddonRoot $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $addonRoot "ofxVlc4360Example\bin\data\360"
}

$presets = @{
    "oceanside-4k" = @{
        FileName = "oceanside-beach-360-4k.webm"
        DownloadUrl = "https://commons.wikimedia.org/wiki/Special:Redirect/file/3D%20360%204K%20Motion%20TIme-lapse%20-%20Oceanside%20Beach.webm"
        SourcePage = "https://commons.wikimedia.org/wiki/File:3D_360_4K_Motion_TIme-lapse_-_Oceanside_Beach.webm"
        Attribution = "Wikimedia Commons / archived Vimeo source, free file page listed above"
        Notes = "Higher quality 3840x2160 360 sample; larger download."
    }
    "dji-mini-2" = @{
        FileName = "dji-mini-2-360-view.webm"
        DownloadUrl = "https://commons.wikimedia.org/wiki/Special:Redirect/file/DJI%20Mini%202%20360%20view%20video.webm"
        SourcePage = "https://commons.wikimedia.org/wiki/File:DJI_Mini_2_360_view_video.webm"
        Attribution = "Wikimedia Commons / Wikideas1 (CC license on source page)"
        Notes = "Smaller, quicker 1920x1080 360 sample."
    }
}

$presetKey = $Preset.ToLowerInvariant()
if (-not $presets.ContainsKey($presetKey)) {
    $supported = ($presets.Keys | Sort-Object) -join ", "
    throw "Unknown preset '$Preset'. Supported presets: $supported"
}

$selected = $presets[$presetKey]
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$targetPath = Join-Path $OutputDir $selected.FileName
if ((Test-Path $targetPath) -and (-not $Force)) {
    Write-Step "File already exists: $targetPath"
    Write-Host "Use -Force to download it again."
    exit 0
}

Write-Step "Downloading preset '$presetKey'"
Write-Host "File: $($selected.FileName)"
Write-Host "Notes: $($selected.Notes)"
Write-Host "Source: $($selected.SourcePage)"
Write-Host "Attribution: $($selected.Attribution)"

Invoke-WebRequest -Uri $selected.DownloadUrl -OutFile $targetPath -Headers @{
    "User-Agent" = "ofxVlc4 360 example downloader"
}

Write-Step "Saved sample media"
Write-Host $targetPath
