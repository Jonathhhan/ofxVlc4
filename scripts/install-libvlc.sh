#!/usr/bin/env bash
set -euo pipefail

HEADER_ZIP_URL="https://github.com/videolan/vlc/archive/refs/heads/master.zip"
ZIP_URL=""
NIGHTLY_INDEX_URL="https://artifacts.videolan.org/vlc/nightly-win64-llvm/"
ADDON_ROOT=""
VLC_APP=""
KEEP_ARCHIVE=0
KEEP_EXTRACTED=0

write_step() {
	printf '==> %s\n' "$1"
}

die() {
	printf '%s\n' "$1" >&2
	exit 1
}

ensure_dir() {
	mkdir -p "$1"
}

reset_dir() {
	rm -rf "$1"
	mkdir -p "$1"
}

require_command() {
	if ! command -v "$1" >/dev/null 2>&1; then
		die "Required command not found: $1"
	fi
}

resolve_addon_root() {
	local start_dir="$1"
	local current
	current="$(cd "$start_dir" && pwd)"
	while [[ -n "$current" ]]; do
		if [[ -d "$current/libs" && -f "$current/addon_config.mk" ]]; then
			printf '%s\n' "$current"
			return 0
		fi

		local parent
		parent="$(dirname "$current")"
		if [[ "$parent" == "$current" ]]; then
			break
		fi
		current="$parent"
	done

	return 1
}

first_existing_dir() {
	local candidate
	for candidate in "$@"; do
		if [[ -n "$candidate" && -d "$candidate" ]]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done
	return 1
}

find_first_file() {
	local root="$1"
	local name="$2"
	find "$root" -type f -name "$name" -print -quit 2>/dev/null || true
}

copy_dir_contents() {
	local source_dir="$1"
	local target_dir="$2"
	if [[ ! -d "$source_dir" ]]; then
		return 0
	fi

	reset_dir "$target_dir"
	cp -R "$source_dir"/. "$target_dir"/
}

resolve_content_root() {
	local extract_root="$1"
	local dirs=()
	while IFS= read -r dir; do
		dirs+=("$dir")
	done < <(find "$extract_root" -mindepth 1 -maxdepth 1 -type d | sort)

	if [[ "${#dirs[@]}" -eq 1 ]]; then
		printf '%s\n' "${dirs[0]}"
	else
		printf '%s\n' "$extract_root"
	fi
}

resolve_include_root() {
	local root="$1"
	first_existing_dir \
		"$root/include/vlc" \
		"$root/sdk/include/vlc"
}

copy_headers_from_include_root() {
	local source_include_dir="$1"
	local target_include_dir="$2"
	local destination="$target_include_dir/vlc"
	ensure_dir "$destination"
	find "$source_include_dir" -maxdepth 1 -type f \( -name '*.h' -o -name '*.hpp' \) -exec cp {} "$destination"/ \;
}

get_example_runtime_targets() {
	local addon_root_path="$1"
	local example_dir
	while IFS= read -r example_dir; do
		printf '%s|%s\n' \
			"$example_dir" \
			"$example_dir/bin/data/libvlc/macos"
	done < <(find "$addon_root_path" -mindepth 1 -maxdepth 1 -type d -name '*Example*' | sort)
}

copy_runtime_to_example_targets() {
	local libvlc_dylib="$1"
	local libvlccore_dylib="$2"
	local plugins_source_root="$3"
	local lua_source_root="$4"
	local addon_root_path="$5"
	local target_line
	while IFS= read -r target_line; do
		[[ -z "$target_line" ]] && continue
		local target_root="${target_line#*|}"
		local lib_target="$target_root/lib"
		local plugins_target="$target_root/plugins"
		local lua_target="$target_root/lua"
		ensure_dir "$lib_target"
		cp "$libvlc_dylib" "$lib_target/"
		cp "$libvlccore_dylib" "$lib_target/"
		copy_dir_contents "$plugins_source_root" "$plugins_target"
		if [[ -n "$lua_source_root" && -d "$lua_source_root" ]]; then
			copy_dir_contents "$lua_source_root" "$lua_target"
		fi
	done < <(get_example_runtime_targets "$addon_root_path")
}

list_macos_dependencies() {
	local file_path="$1"
	otool -L "$file_path" | tail -n +2 | awk '{print $1}'
}

relative_path() {
	local from_dir="$1"
	local to_dir="$2"
	local from_abs to_abs common suffix back_path
	from_abs="$(cd "$from_dir" && pwd)"
	to_abs="$(cd "$to_dir" && pwd)"
	common="$from_abs"
	back_path=""

	while [[ "${to_abs#"$common"}" == "$to_abs" ]]; do
		back_path="../${back_path}"
		common="$(dirname "$common")"
		if [[ "$common" == "/" ]]; then
			break
		fi
	done

	suffix="${to_abs#"$common"/}"
	printf '%s%s\n' "$back_path" "$suffix"
}

rewrite_dependency_if_present() {
	local file_path="$1"
	local desired_basename="$2"
	local replacement_path="$3"
	local dependency
	while IFS= read -r dependency; do
		if [[ "$(basename "$dependency")" == "$desired_basename" ]]; then
			install_name_tool -change "$dependency" "$replacement_path" "$file_path"
		fi
	done < <(list_macos_dependencies "$file_path")
}

rewrite_runtime_install_names() {
	local runtime_root="$1"
	local lib_dir="$runtime_root/lib"
	local binary_path binary_dir relative_lib_dir

	install_name_tool -id "@rpath/libvlc.dylib" "$lib_dir/libvlc.dylib"
	install_name_tool -id "@rpath/libvlccore.dylib" "$lib_dir/libvlccore.dylib"
	rewrite_dependency_if_present "$lib_dir/libvlc.dylib" "libvlccore.dylib" "@rpath/libvlccore.dylib"

	while IFS= read -r binary_path; do
		if [[ "$binary_path" == "$lib_dir/libvlc.dylib" || "$binary_path" == "$lib_dir/libvlccore.dylib" ]]; then
			continue
		fi

		binary_dir="$(dirname "$binary_path")"
		if [[ "$binary_path" == "$lib_dir/"* ]]; then
			rewrite_dependency_if_present "$binary_path" "libvlc.dylib" "@rpath/libvlc.dylib"
			rewrite_dependency_if_present "$binary_path" "libvlccore.dylib" "@rpath/libvlccore.dylib"
		else
			relative_lib_dir="$(relative_path "$binary_dir" "$lib_dir")"
			rewrite_dependency_if_present "$binary_path" "libvlc.dylib" "@loader_path/${relative_lib_dir}/libvlc.dylib"
			rewrite_dependency_if_present "$binary_path" "libvlccore.dylib" "@loader_path/${relative_lib_dir}/libvlccore.dylib"
		fi
	done < <(find "$runtime_root" -type f -name '*.dylib' | sort)
}

to_windows_path() {
	local input_path="$1"
	if command -v cygpath >/dev/null 2>&1; then
		cygpath -w "$input_path"
	elif command -v wslpath >/dev/null 2>&1; then
		wslpath -w "$input_path"
	else
		die "Could not convert path to Windows form: $input_path"
	fi
}

run_macos_install() {
	local script_dir
	script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
	if [[ -z "$ADDON_ROOT" ]]; then
		ADDON_ROOT="$(resolve_addon_root "$script_dir")" || die "Could not determine addon root from '$script_dir'."
	fi

	if [[ -z "$VLC_APP" ]]; then
		VLC_APP="$(first_existing_dir \
			"/Applications/VLC.app" \
			"/Applications/VLC nightly.app" \
			"/Applications/VLC-4.0.app")" || true
	fi

	[[ -n "$VLC_APP" && -d "$VLC_APP" ]] || die "Could not find VLC.app. Pass it explicitly with --vlc-app /path/to/VLC.app."

	require_command curl
	require_command unzip
	require_command otool
	require_command install_name_tool

	write_step "Preparing install paths"

	local libvlc_root target_include_dir target_mac_root target_lib_dir target_plugins_dir target_lua_dir
	libvlc_root="$ADDON_ROOT/libs/libvlc"
	target_include_dir="$libvlc_root/include"
	target_mac_root="$libvlc_root/lib/osx"
	target_lib_dir="$target_mac_root/lib"
	target_plugins_dir="$target_mac_root/plugins"
	target_lua_dir="$target_mac_root/lua"

	local temp_root header_archive_path header_extract_path
	temp_root="$(mktemp -d "${TMPDIR:-/tmp}/ofxVlc4-libvlc-macos-XXXXXX")"
	header_archive_path="$temp_root/vlc-headers.zip"
	header_extract_path="$temp_root/headers-extract"

	ensure_dir "$header_extract_path"
	ensure_dir "$target_lib_dir"

	local vlc_contents lib_source_root plugins_source_root lua_source_root libvlc_dylib libvlccore_dylib
	vlc_contents="$VLC_APP/Contents"
	lib_source_root="$(first_existing_dir "$vlc_contents/MacOS/lib" "$vlc_contents/Frameworks")" || die "Could not find a libvlc runtime folder inside '$VLC_APP'."
	plugins_source_root="$(first_existing_dir "$vlc_contents/Frameworks/plugins" "$vlc_contents/MacOS/plugins")" || die "Could not find a VLC plugins folder inside '$VLC_APP'."
	lua_source_root="$(first_existing_dir "$vlc_contents/Frameworks/lua" "$vlc_contents/MacOS/lua")" || true

	libvlc_dylib="$(find_first_file "$lib_source_root" 'libvlc.dylib')"
	libvlccore_dylib="$(find_first_file "$lib_source_root" 'libvlccore.dylib')"

	[[ -n "$libvlc_dylib" && -f "$libvlc_dylib" ]] || die "Could not find libvlc.dylib inside '$VLC_APP'."
	[[ -n "$libvlccore_dylib" && -f "$libvlccore_dylib" ]] || die "Could not find libvlccore.dylib inside '$VLC_APP'."

	write_step "Downloading VLC headers from GitHub master"
	printf '     %s\n' "$HEADER_ZIP_URL"
	curl -L --fail --output "$header_archive_path" "$HEADER_ZIP_URL"

	write_step "Extracting VLC headers"
	unzip -q "$header_archive_path" -d "$header_extract_path"
	local header_content_root include_root
	header_content_root="$(resolve_content_root "$header_extract_path")"
	include_root="$(resolve_include_root "$header_content_root")"
	[[ -n "$include_root" && -d "$include_root" ]] || die "Could not find the public libvlc header directory in the downloaded archive."

	write_step "Installing headers and macOS runtime into addon libs/libvlc"
	reset_dir "$target_include_dir"
	copy_headers_from_include_root "$include_root" "$target_include_dir"

	ensure_dir "$target_lib_dir"
	cp "$libvlc_dylib" "$target_lib_dir/libvlc.dylib"
	cp "$libvlccore_dylib" "$target_lib_dir/libvlccore.dylib"
	copy_dir_contents "$plugins_source_root" "$target_plugins_dir"
	if [[ -n "$lua_source_root" && -d "$lua_source_root" ]]; then
		copy_dir_contents "$lua_source_root" "$target_lua_dir"
	else
		rm -rf "$target_lua_dir"
	fi

	write_step "Rewriting macOS install names in addon runtime"
	rewrite_runtime_install_names "$target_mac_root"

	write_step "Copying macOS runtime into example data folders"
	copy_runtime_to_example_targets "$target_lib_dir/libvlc.dylib" "$target_lib_dir/libvlccore.dylib" "$target_plugins_dir" "$target_lua_dir" "$ADDON_ROOT"

	if [[ "$KEEP_ARCHIVE" -eq 0 ]]; then
		rm -f "$header_archive_path"
	fi

	if [[ "$KEEP_EXTRACTED" -eq 0 ]]; then
		rm -rf "$temp_root"
	fi

	write_step "Done"
	printf '\n'
	printf 'Installed macOS libvlc into:\n'
	printf '  headers: %s\n' "$target_include_dir"
	printf '  runtime: %s\n' "$target_mac_root"
	printf '  source app: %s\n' "$VLC_APP"
}

run_windows_install() {
	require_command powershell.exe

	if [[ -z "$ADDON_ROOT" ]]; then
		local script_dir
		script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
		ADDON_ROOT="$(resolve_addon_root "$script_dir")" || die "Could not determine addon root from '$script_dir'."
	fi

	local temp_root ps_script ps_script_win addon_root_win
	temp_root="$(mktemp -d "${TMPDIR:-/tmp}/ofxVlc4-libvlc-shell.XXXXXXXX")"
	ps_script="${temp_root}/install-libvlc.ps1"
	trap 'rm -rf "'"${temp_root}"'"' EXIT

	cat > "${ps_script}" <<'EOF'
[CmdletBinding()]
param(
	[string]$ZipUrl = "",
	[string]$NightlyIndexUrl = "https://artifacts.videolan.org/vlc/nightly-win64-llvm/",
	[string]$HeaderZipUrl = "https://github.com/videolan/vlc/archive/refs/heads/master.zip",
	[string]$AddonRoot = "",
	[switch]$KeepArchive,
	[switch]$KeepExtracted
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-AddonRoot([string]$StartDirectory) {
	$Current = Resolve-Path -LiteralPath $StartDirectory
	while ($null -ne $Current) {
		$Candidate = $Current.Path
		if ((Test-Path -LiteralPath (Join-Path $Candidate 'libs')) -and (Test-Path -LiteralPath (Join-Path $Candidate 'addon_config.mk'))) {
			return $Candidate
		}

		$Parent = Split-Path -Parent $Candidate
		if ([string]::IsNullOrWhiteSpace($Parent) -or $Parent -eq $Candidate) {
			break
		}
		$Current = Resolve-Path -LiteralPath $Parent
	}

	throw "Could not determine addon root from '$StartDirectory'."
}

function Write-Step([string]$Message) {
	Write-Host "==> $Message" -ForegroundColor Cyan
}

function Ensure-Directory([string]$Path) {
	if (-not (Test-Path -LiteralPath $Path)) {
		New-Item -ItemType Directory -Path $Path | Out-Null
	}
}

function Reset-Directory([string]$Path) {
	if (Test-Path -LiteralPath $Path) {
		Remove-Item -LiteralPath $Path -Recurse -Force
	}
	New-Item -ItemType Directory -Path $Path | Out-Null
}

function Find-FirstPath([string[]]$Candidates) {
	foreach ($Candidate in $Candidates) {
		if (-not [string]::IsNullOrWhiteSpace($Candidate) -and (Test-Path -LiteralPath $Candidate)) {
			return (Resolve-Path -LiteralPath $Candidate).Path
		}
	}
	return $null
}

function Find-FirstFileByName([string]$Root, [string]$Name) {
	if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path -LiteralPath $Root)) {
		return $null
	}

	$Match = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Name -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($null -eq $Match) {
		return $null
	}
	return $Match.FullName
}

function Copy-OptionalFile([string]$Source, [string]$DestinationDirectory) {
	if ([string]::IsNullOrWhiteSpace($Source) -or -not (Test-Path -LiteralPath $Source)) {
		return
	}
	Copy-Item -LiteralPath $Source -Destination (Join-Path $DestinationDirectory (Split-Path $Source -Leaf)) -Force
}

function Resolve-ContentRoot([string]$ExtractRoot) {
	$TopLevelDirectories = @(Get-ChildItem -LiteralPath $ExtractRoot -Directory -ErrorAction SilentlyContinue)
	if ($TopLevelDirectories.Count -eq 1) {
		return $TopLevelDirectories[0].FullName
	}
	return $ExtractRoot
}

function Resolve-IncludeRoot([string]$Root) {
	if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path -LiteralPath $Root)) {
		return $null
	}

	return Find-FirstPath @(
		(Join-Path $Root 'include\vlc'),
		(Join-Path $Root 'sdk\include\vlc')
	)
}

function Copy-DirectoryContents([string]$SourceDirectory, [string]$TargetDirectory) {
	if ([string]::IsNullOrWhiteSpace($SourceDirectory) -or -not (Test-Path -LiteralPath $SourceDirectory)) {
		return
	}

	Reset-Directory $TargetDirectory
	Copy-Item -Path (Join-Path $SourceDirectory '*') -Destination $TargetDirectory -Recurse -Force
}

function Copy-HeadersFromIncludeRoot([string]$SourceIncludeDirectory, [string]$TargetIncludeDirectory) {
	$HeaderFiles = Get-ChildItem -LiteralPath $SourceIncludeDirectory -File |
		Where-Object { $_.Extension -in @('.h', '.hpp') }

	$Destination = Join-Path $TargetIncludeDirectory 'vlc'
	Ensure-Directory $Destination
	foreach ($HeaderFile in $HeaderFiles) {
		Copy-Item -LiteralPath $HeaderFile.FullName -Destination (Join-Path $Destination $HeaderFile.Name) -Force
	}
}

function Get-ExampleRuntimeTargets([string]$AddonRootPath) {
	return @(Get-ChildItem -LiteralPath $AddonRootPath -Directory -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -like '*Example*' } |
		ForEach-Object {
			[pscustomobject]@{
				ExampleRoot = $_.FullName
				BinDirectory = Join-Path $_.FullName 'bin'
				DllDirectory = Join-Path $_.FullName 'dll\x64'
			}
		})
}

function Copy-RuntimeToExampleTargets([string]$LibvlcDll, [string]$LibvlccoreDll, [string]$PluginsSourceRoot, [string]$LuaSourceRoot, [object[]]$ExampleRuntimeTargets) {
	foreach ($Target in $ExampleRuntimeTargets) {
		Ensure-Directory $Target.BinDirectory
		Copy-OptionalFile $LibvlcDll $Target.BinDirectory
		Copy-OptionalFile $LibvlccoreDll $Target.BinDirectory
		Copy-DirectoryContents $PluginsSourceRoot (Join-Path $Target.BinDirectory 'plugins')
		Copy-DirectoryContents $LuaSourceRoot (Join-Path $Target.BinDirectory 'lua')

		Ensure-Directory $Target.DllDirectory
		Copy-OptionalFile $LibvlcDll $Target.DllDirectory
		Copy-OptionalFile $LibvlccoreDll $Target.DllDirectory
		Copy-DirectoryContents $PluginsSourceRoot (Join-Path $Target.DllDirectory 'plugins')
		Copy-DirectoryContents $LuaSourceRoot (Join-Path $Target.DllDirectory 'lua')
	}
}

function Resolve-LatestNightlyZipUrl([string]$IndexUrl) {
	$IndexResponse = Invoke-WebRequest -UseBasicParsing -Uri $IndexUrl
	$NightlyMatches = [regex]::Matches($IndexResponse.Content, 'href="(\d{8}-\d{4}/)"')
	if ($NightlyMatches.Count -eq 0) {
		throw "Could not find a nightly build directory at '$IndexUrl'."
	}

	$NightlyRelativePath = ($NightlyMatches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Descending | Select-Object -First 1)
	$NightlyDirectoryUrl = [System.Uri]::new([System.Uri]$IndexUrl, $NightlyRelativePath).AbsoluteUri

	$NightlyResponse = Invoke-WebRequest -UseBasicParsing -Uri $NightlyDirectoryUrl
	$ZipMatches = [regex]::Matches($NightlyResponse.Content, 'href="(vlc-.*-win64-.*\.zip)"')
	if ($ZipMatches.Count -eq 0) {
		throw "Could not find a nightly ZIP inside '$NightlyDirectoryUrl'."
	}

	$ZipRelativePath = $ZipMatches[0].Groups[1].Value
	return [System.Uri]::new([System.Uri]$NightlyDirectoryUrl, $ZipRelativePath).AbsoluteUri
}

function Find-VsToolPaths() {
	$ToolNames = @('dumpbin.exe', 'lib.exe')
	$SearchRoots = @(
		'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64',
		'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\MSVC\14.50.35717\bin\HostX86\x64'
	)

	$Paths = @{}
	foreach ($ToolName in $ToolNames) {
		$Command = Get-Command $ToolName -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($null -ne $Command -and -not [string]::IsNullOrWhiteSpace($Command.Source)) {
			$Paths[$ToolName] = $Command.Source
			continue
		}

		$Candidates = @($SearchRoots | ForEach-Object { Join-Path $_ $ToolName })
		$Resolved = Find-FirstPath $Candidates
		if (-not [string]::IsNullOrWhiteSpace($Resolved)) {
			$Paths[$ToolName] = $Resolved
		}
	}
	return $Paths
}

function New-ImportLibraryFromDll([string]$DllPath, [string]$OutputLibPath, [string]$TempDirectory) {
	$ToolPaths = Find-VsToolPaths
	$DumpbinPath = $ToolPaths['dumpbin.exe']
	$LibExePath = $ToolPaths['lib.exe']
	if ([string]::IsNullOrWhiteSpace($DumpbinPath) -or [string]::IsNullOrWhiteSpace($LibExePath)) {
		throw "Could not find dumpbin.exe and lib.exe to generate $(Split-Path $OutputLibPath -Leaf)."
	}

	$BaseName = [System.IO.Path]::GetFileNameWithoutExtension($DllPath)
	$DefPath = Join-Path $TempDirectory ($BaseName + '.def')
	$ExportLines = & $DumpbinPath /exports $DllPath 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "dumpbin.exe failed while reading exports from $(Split-Path $DllPath -Leaf)."
	}

	$ExportNames = New-Object System.Collections.Generic.List[string]
	$InExportsTable = $false
	foreach ($Line in $ExportLines) {
		if ($Line -match '^\s+ordinal\s+hint\s+RVA\s+name$') {
			$InExportsTable = $true
			continue
		}
		if (-not $InExportsTable) { continue }
		if ($Line -match '^\s*Summary$') { break }
		if ($Line -match '^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(.+)$') {
			$Name = $Matches[1].Trim()
			$Forwarder = $Name.IndexOf('=')
			if ($Forwarder -ge 0) {
				$Name = $Name.Substring(0, $Forwarder).Trim()
			}
			if (-not [string]::IsNullOrWhiteSpace($Name)) {
				$ExportNames.Add($Name)
			}
		}
	}

	if ($ExportNames.Count -eq 0) {
		throw "Could not extract any exports from $(Split-Path $DllPath -Leaf)."
	}

	(@("LIBRARY $(Split-Path $DllPath -Leaf)", 'EXPORTS') + $ExportNames) | Set-Content -Path $DefPath
	& $LibExePath /def:$DefPath /machine:x64 /out:$OutputLibPath | Out-Null
	if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutputLibPath)) {
		throw "lib.exe failed while generating $(Split-Path $OutputLibPath -Leaf)."
	}
}

function Remove-StaleRuntimeFromLibraryDirectory([string]$TargetLibraryDirectory) {
	$StaleRuntimeFiles = @(
		(Join-Path $TargetLibraryDirectory 'libvlc.dll'),
		(Join-Path $TargetLibraryDirectory 'libvlccore.dll')
	)
	foreach ($StaleRuntimeFile in $StaleRuntimeFiles) {
		if (Test-Path -LiteralPath $StaleRuntimeFile) {
			Remove-Item -LiteralPath $StaleRuntimeFile -Force
		}
	}

	foreach ($StaleRuntimeDirectory in @((Join-Path $TargetLibraryDirectory 'plugins'), (Join-Path $TargetLibraryDirectory 'lua'))) {
		if (Test-Path -LiteralPath $StaleRuntimeDirectory) {
			Remove-Item -LiteralPath $StaleRuntimeDirectory -Recurse -Force
		}
	}
}

if ([string]::IsNullOrWhiteSpace($AddonRoot)) {
	$ScriptDirectory = if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
	if ([string]::IsNullOrWhiteSpace($ScriptDirectory)) {
		throw 'Could not determine the script directory.'
	}
	$AddonRoot = Resolve-AddonRoot $ScriptDirectory
}

Write-Step 'Preparing install paths'

$LibVlcRoot = Join-Path $AddonRoot 'libs\libvlc'
$TargetIncludeDirectory = Join-Path $LibVlcRoot 'include'
$TargetLibraryDirectory = Join-Path $LibVlcRoot 'lib\vs'
$ExampleRuntimeTargets = Get-ExampleRuntimeTargets $AddonRoot

$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('ofxVlc4-libvlc-' + [guid]::NewGuid().ToString('N'))
$ArchivePath = Join-Path $TempRoot 'libvlc.zip'
$ExtractPath = Join-Path $TempRoot 'extract'
$HeaderArchivePath = Join-Path $TempRoot 'vlc-headers.zip'
$HeaderExtractPath = Join-Path $TempRoot 'headers-extract'
$GeneratedLibDirectory = Join-Path $TempRoot 'generated-libs'

Ensure-Directory $TempRoot
Ensure-Directory $ExtractPath
Ensure-Directory $HeaderExtractPath
Ensure-Directory $GeneratedLibDirectory

if ([string]::IsNullOrWhiteSpace($ZipUrl)) {
	Write-Step 'Resolving latest nightly ZIP'
	$ZipUrl = Resolve-LatestNightlyZipUrl $NightlyIndexUrl
}

Write-Step 'Downloading VLC archive'
Write-Host "     $ZipUrl"
Invoke-WebRequest -UseBasicParsing -Uri $ZipUrl -OutFile $ArchivePath

Write-Step 'Extracting archive'
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $ExtractPath -Force
$ContentRoot = Resolve-ContentRoot $ExtractPath

Write-Step 'Downloading VLC headers from GitHub master'
Write-Host "     $HeaderZipUrl"
Invoke-WebRequest -UseBasicParsing -Uri $HeaderZipUrl -OutFile $HeaderArchivePath

Write-Step 'Extracting VLC headers'
Expand-Archive -LiteralPath $HeaderArchivePath -DestinationPath $HeaderExtractPath -Force
$IncludeRoot = Resolve-IncludeRoot (Resolve-ContentRoot $HeaderExtractPath)
if ([string]::IsNullOrWhiteSpace($IncludeRoot)) {
	throw 'Could not find the public libvlc header directory in the GitHub master archive.'
}

$LibvlcDll = Find-FirstFileByName $ContentRoot 'libvlc.dll'
$LibvlccoreDll = Find-FirstFileByName $ContentRoot 'libvlccore.dll'
$PluginsSourceRoot = Find-FirstPath @((Join-Path $ContentRoot 'plugins'))
$LuaSourceRoot = Find-FirstPath @((Join-Path $ContentRoot 'lua'))

if ([string]::IsNullOrWhiteSpace($LibvlcDll) -or [string]::IsNullOrWhiteSpace($LibvlccoreDll)) {
	throw 'Could not find libvlc.dll and libvlccore.dll in the downloaded archive.'
}

$LibvlcImportLibrary = Find-FirstFileByName $ContentRoot 'libvlc.lib'
if ([string]::IsNullOrWhiteSpace($LibvlcImportLibrary)) {
	Write-Step 'Generating libvlc.lib from libvlc.dll'
	$LibvlcImportLibrary = Join-Path $GeneratedLibDirectory 'libvlc.lib'
	New-ImportLibraryFromDll $LibvlcDll $LibvlcImportLibrary $GeneratedLibDirectory
}

Write-Step 'Installing headers and runtime into addon libs/libvlc'
Reset-Directory $TargetIncludeDirectory
Ensure-Directory $TargetLibraryDirectory
Copy-HeadersFromIncludeRoot $IncludeRoot $TargetIncludeDirectory
Copy-Item -LiteralPath $LibvlcImportLibrary -Destination (Join-Path $TargetLibraryDirectory 'libvlc.lib') -Force
Remove-StaleRuntimeFromLibraryDirectory $TargetLibraryDirectory

Write-Step 'Copying VLC runtime into example bin folders'
Copy-RuntimeToExampleTargets $LibvlcDll $LibvlccoreDll $PluginsSourceRoot $LuaSourceRoot $ExampleRuntimeTargets

if (-not $KeepArchive) {
	if (Test-Path -LiteralPath $ArchivePath) { Remove-Item -LiteralPath $ArchivePath -Force }
	if (Test-Path -LiteralPath $HeaderArchivePath) { Remove-Item -LiteralPath $HeaderArchivePath -Force }
}

if (-not $KeepExtracted -and (Test-Path -LiteralPath $TempRoot)) {
	Remove-Item -LiteralPath $TempRoot -Recurse -Force
}

Write-Step 'Done'
Write-Host ''
Write-Host 'Installed libvlc into:' -ForegroundColor Green
Write-Host "  headers: $TargetIncludeDirectory"
Write-Host "  import lib: $TargetLibraryDirectory"
if ($ExampleRuntimeTargets.Count -gt 0) {
	Write-Host '  runtime copied to example folders:'
	foreach ($ExampleRuntimeTarget in $ExampleRuntimeTargets) {
		Write-Host "    bin: $($ExampleRuntimeTarget.BinDirectory)"
		Write-Host "    dll: $($ExampleRuntimeTarget.DllDirectory)"
	}
}
EOF

	ps_script_win="$(to_windows_path "${ps_script}")"
	addon_root_win="$(to_windows_path "${ADDON_ROOT}")"

	local ps_args=(
		-ExecutionPolicy Bypass
		-File "${ps_script_win}"
		-AddonRoot "${addon_root_win}"
		-NightlyIndexUrl "${NIGHTLY_INDEX_URL}"
		-HeaderZipUrl "${HEADER_ZIP_URL}"
	)

	if [[ -n "${ZIP_URL}" ]]; then
		ps_args+=(-ZipUrl "${ZIP_URL}")
	fi
	if [[ "${KEEP_ARCHIVE}" -eq 1 ]]; then
		ps_args+=(-KeepArchive)
	fi
	if [[ "${KEEP_EXTRACTED}" -eq 1 ]]; then
		ps_args+=(-KeepExtracted)
	fi

	powershell.exe "${ps_args[@]}"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--addon-root)
			ADDON_ROOT="${2:-}"
			shift 2
			;;
		--vlc-app)
			VLC_APP="${2:-}"
			shift 2
			;;
		--zip-url)
			ZIP_URL="${2:-}"
			shift 2
			;;
		--nightly-index-url)
			NIGHTLY_INDEX_URL="${2:-}"
			shift 2
			;;
		--header-zip-url)
			HEADER_ZIP_URL="${2:-}"
			shift 2
			;;
		--keep-archive)
			KEEP_ARCHIVE=1
			shift
			;;
		--keep-extracted)
			KEEP_EXTRACTED=1
			shift
			;;
		*)
			die "Unknown argument: $1"
			;;
	esac
done

case "$(uname -s)" in
	Darwin)
		run_macos_install
		;;
	Linux)
		if command -v powershell.exe >/dev/null 2>&1; then
			run_windows_install
		else
			die "Native Linux uses system libVLC packages; see README for the pkg-config setup."
		fi
		;;
	MINGW*|MSYS*|CYGWIN*)
		run_windows_install
		;;
	*)
		die "Unsupported platform: $(uname -s)"
		;;
esac
