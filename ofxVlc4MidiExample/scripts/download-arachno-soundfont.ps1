$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$soundFontDir = Join-Path $projectRoot "bin\data\soundfonts"

New-Item -ItemType Directory -Force $soundFontDir | Out-Null
$zipPath = Join-Path $soundFontDir "Arachno.zip"
$targetPath = Join-Path $soundFontDir "Arachno.sf2"
$downloadUrls = @(
	"https://www.dropbox.com/scl/fi/o7ctduftiauo5sivcani2/arachno-soundfont-10-sf2.zip?rlkey=l55b9ry7z4bt20htue35joi9z&dl=1",
	"https://maxime.abbey.free.fr/mirror/arachnosoft/files/soundfonts/arachno-soundfont-10-sf2.zip"
)

Write-Host "Downloading Arachno SoundFont..."
Write-Host "Source mirrors:"
$downloadUrls | ForEach-Object { Write-Host "  $_" }
Write-Host "Target: $targetPath"

if (Test-Path $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

$downloaded = $false
foreach ($downloadUrl in $downloadUrls) {
	try {
		Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath
		$downloaded = $true
		break
	} catch {
		Write-Host "Mirror failed: $downloadUrl"
	}
}

if (-not $downloaded) {
	throw "Unable to download Arachno SoundFont from the configured official mirrors."
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
	$sf2Entry = $archive.Entries |
		Where-Object { -not [string]::IsNullOrEmpty($_.Name) -and $_.FullName.ToLowerInvariant().EndsWith(".sf2") } |
		Select-Object -First 1
	if (-not $sf2Entry) {
		throw "Downloaded Arachno archive did not contain an .sf2 file."
	}

	if (Test-Path $targetPath) {
		Remove-Item -LiteralPath $targetPath -Force
	}

	[System.IO.Compression.ZipFileExtensions]::ExtractToFile($sf2Entry, $targetPath)
} finally {
	$archive.Dispose()
}
Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue

$file = Get-Item $targetPath
Write-Host ("Done. Saved {0} ({1:N1} MB)." -f $file.FullName, ($file.Length / 1MB))
