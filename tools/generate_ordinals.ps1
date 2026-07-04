# Generate ordinal tables from binkw32 DLL exports using dumpbin
# Output: src/ordinals.inc (all tables) + tools/ordinals_map.json (version→group mapping)
#
# Usage (from project root):
#   powershell -ExecutionPolicy Bypass -File tools/generate_ordinals.ps1
#
# Usage (with custom paths):
#   powershell -ExecutionPolicy Bypass -File tools/generate_ordinals.ps1 -BinkDir path/to/dlls -OutDir path/to/src

param(
    [string]$BinkDir = "Real",
    [string]$OutDir = "src"
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

# Resolve relative paths from project root
if (-not [System.IO.Path]::IsPathRooted($BinkDir)) { $BinkDir = Join-Path $projectRoot $BinkDir }
if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $projectRoot $OutDir }

# Find dumpbin automatically
$dumpbin = $null
$msvcVersions = @("18", "17", "16")
foreach ($ver in $msvcVersions) {
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\$ver\Enterprise\VC\Tools\MSVC\*\bin\Hostx64\x86\dumpbin.exe",
        "C:\Program Files\Microsoft Visual Studio\$ver\Professional\VC\Tools\MSVC\*\bin\Hostx64\x86\dumpbin.exe",
        "C:\Program Files\Microsoft Visual Studio\$ver\Community\VC\Tools\MSVC\*\bin\Hostx64\x86\dumpbin.exe"
    )
    foreach ($pattern in $candidates) {
        $found = Get-Item $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { $dumpbin = $found.FullName; break }
    }
    if ($dumpbin) { break }
}
if (-not $dumpbin) {
    # Try bundled dumpbin (from tools/dumpbin/)
    $bundled = Join-Path $scriptDir "dumpbin\dumpbin.exe"
    if (Test-Path $bundled) { $dumpbin = $bundled }
}
if (-not $dumpbin) {
    Write-Host "dumpbin.exe not found. Downloading from GitHub..." -ForegroundColor Yellow
    & (Join-Path $scriptDir "setup.ps1") -Tools dumpbin
    $bundled = Join-Path $scriptDir "dumpbin\dumpbin.exe"
    if (Test-Path $bundled) { $dumpbin = $bundled }
}
if (-not $dumpbin) {
    Write-Error "dumpbin.exe not found. Install Visual Studio or run: tools\setup.ps1"
    exit 1
}
Write-Host "Using dumpbin: $dumpbin"

# All known Bink function names we care about
$knownFunctions = @(
    "BinkLogoAddress", "BinkSetError", "BinkGetError", "BinkOpen", "BinkOpenWithOptions",
    "BinkDoFrame", "BinkDoFramePlane", "BinkNextFrame", "BinkWait", "BinkClose",
    "BinkPause", "BinkCopyToBuffer", "BinkCopyToBufferRect", "BinkGetRects",
    "BinkGoto", "BinkGetKeyFrame", "BinkFreeGlobals", "BinkGetPlatformInfo",
    "BinkGetFrameBuffersInfo", "BinkRegisterFrameBuffers", "BinkSetVideoOnOff",
    "BinkSetSoundOnOff", "BinkSetVolume", "BinkSetPan", "BinkSetSpeakerVolumes",
    "BinkService", "BinkShouldSkip", "BinkGetPalette", "BinkControlBackgroundIO",
    "BinkControlPlatformFeatures", "BinkSetWillLoop", "BinkOpenTrack", "BinkCloseTrack",
    "BinkGetTrackData", "BinkGetTrackType", "BinkGetTrackMaxSize", "BinkGetTrackID",
    "BinkGetSummary", "BinkGetRealtime", "BinkSetFileOffset", "BinkSetSoundTrack",
    "BinkSetIO", "BinkSetFrameRate", "BinkSetSimulate", "BinkSetIOSize",
    "BinkSetSoundSystem", "BinkOpenDirectSound", "BinkOpenWaveOut", "BinkOpenMiles",
    "BinkDX8SurfaceType", "BinkDX9SurfaceType", "BinkBufferOpen", "BinkBufferSetHWND",
    "BinkDDSurfaceType", "BinkIsSoftwareCursor", "BinkCheckCursor",
    "BinkBufferSetDirectDraw", "BinkBufferClose", "BinkBufferLock", "BinkBufferUnlock",
    "BinkBufferSetResolution", "BinkBufferCheckWinPos", "BinkBufferSetOffset",
    "BinkBufferBlit", "BinkBufferSetScale", "BinkBufferGetDescription",
    "BinkBufferGetError", "BinkBufferClear", "BinkRestoreCursor",
    "BinkStartAsyncThread", "BinkDoFrameAsync", "BinkDoFrameAsyncWait",
    "BinkRequestStopAsyncThread", "BinkWaitStopAsyncThread",
    "BinkSetMixBins", "BinkSetMixBinVolumes", "ExpandBink", "ExpandBundleSizes",
    "RADSetMemory", "RADTimerRead", "radmalloc", "radfree",
    "BinkSetMemory", "BinkSetSoundTrack8", "BinkSetSoundTrack4",
    "BinkSetVolume2",
    "YUV_init",
    "YUV_blit_16a1bpp", "YUV_blit_16a1bpp_mask", "YUV_blit_16a4bpp", "YUV_blit_16a4bpp_mask",
    "YUV_blit_16bpp", "YUV_blit_16bpp_mask", "YUV_blit_24bpp", "YUV_blit_24bpp_mask",
    "YUV_blit_24rbpp", "YUV_blit_24rbpp_mask", "YUV_blit_32abpp", "YUV_blit_32abpp_mask",
    "YUV_blit_32bpp", "YUV_blit_32bpp_mask", "YUV_blit_32rabpp", "YUV_blit_32rabpp_mask",
    "YUV_blit_32rbpp", "YUV_blit_32rbpp_mask", "YUV_blit_UYVY", "YUV_blit_UYVY_mask",
    "YUV_blit_YUY2", "YUV_blit_YUY2_mask", "YUV_blit_YV12",
    "BinkOpenXAudio2", "BinkOpenXAudio27", "BinkOpenXAudio28",
    "BinkDoFrameAsyncMulti", "BinkRequestStopAsyncThreadsMulti",
    "BinkWaitStopAsyncThreadsMulti", "BinkAllocateFrameBuffers",
    "BinkGetGPUDataBuffersInfo", "BinkRegisterGPUDataBuffers",
    "BinkSetOSFileCallbacks", "BinkSetLowLevelFileCallbacks",
    "BinkSetSoundSystem2", "BinkUtilCPUs", "BinkUtilFree", "BinkUtilMalloc",
    "BinkUtilMutexCreate", "BinkUtilMutexDestroy", "BinkUtilMutexLock",
    "BinkUtilMutexLockTimeOut", "BinkUtilMutexUnlock",
    "BinkUseTelemetry", "BinkUseTmLite", "BinkServiceSound",
    "BinkMake", "BinkMakeClose", "BinkMakeFlush", "BinkMakePutVideo", "BinkMix"
)

# Skip non-video DLLs and incompatible versions
# 0.5a-0.9x: Too old, crashes internally
# 1.0c-1.0f: BinkOpen returns NULL (can't open RA2YR video files)
# 1.2h: Crashes after BinkSetSoundSystem
# 1.8r: BinkMake/BinkMix tool, missing all 13 needed functions
# 1.99a-1.99w, 2.1c: Pre-release builds, crash after BinkOpen
# 2.4i: Bink 2.x, different internal implementation
# 2.7g: Bink 2.x, missing BinkDDSurfaceType
$skipVersions = @("0.5a", "0.8a", "0.8e", "0.8f", "0.8h", "0.8i", "0.9d", "0.9f", "0.9g", "0.9i", "0.9j", "0.9k", "0.9m", "0.9n", "1.0c", "1.0d", "1.0f", "1.2h", "1.8r", "1.99a", "1.99b", "1.99d", "1.99f", "1.99g", "1.99L", "1.99m", "1.99n", "1.99p", "1.99r", "1.99t", "1.99v", "1.99w", "1.9y", "1.9z", "2.1c", "2.4i", "2.7g")

# Extract exports from a single DLL
function Get-Exports($dllPath) {
    $output = & $dumpbin /exports $dllPath 2>&1
    $exports = @{}
    foreach ($line in $output) {
        if ($line -match '^\s+(\d+)\s+\w+\s+\w+\s+_(\w+?)(?:@\d+)?$') {
            $ordinal = [int]$Matches[1]
            $name = $Matches[2]
            $exports[$ordinal] = $name
        }
    }
    return $exports
}

# Build signature string from exports (for grouping)
function Get-Signature($exports) {
    return ($exports.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join "|"
}

# Analyze all DLLs
$files = Get-ChildItem "$BinkDir\*.dll" | Sort-Object Name
if ($files.Count -eq 0) {
    Write-Error "No DLLs found in $BinkDir"
    exit 1
}
Write-Host "Analyzing $($files.Count) DLLs in $BinkDir..."

$groups = @{}
$versionMap = @{}

foreach ($f in $files) {
    $baseName = $f.BaseName
    # Extract version: "binkw32_1.0q" -> "1.0q", "1.0q-binkw32" -> "1.0q"
    if ($baseName -match '^binkw?32_(.+)$') { $version = $Matches[1] }
    elseif ($baseName -match '^(.+)-binkw?32$') { $version = $Matches[1] }
    else { $version = $baseName }
    if ($version -in $skipVersions) { continue }
    $exports = Get-Exports $f.FullName
    if ($exports.Count -eq 0) { continue }

    $sig = Get-Signature $exports
    if (-not $groups.ContainsKey($sig)) {
        $groups[$sig] = @{ exports = $exports; versions = @() }
    }
    $groups[$sig].versions += $version
    $versionMap[$version] = $sig
}

# Assign group numbers
$groupNum = 0
$groupMap = @{}
$sortedGroups = $groups.GetEnumerator() | Sort-Object { $_.Value.versions.Count } -Descending

foreach ($entry in $sortedGroups) {
    $groupNum++
    $groupMap[$entry.Key] = $groupNum
    foreach ($v in $entry.Value.versions) {
        $versionMap[$v] = $groupNum
    }
}

# Generate ordinals.inc
$incContent = @"
// Auto-generated by tools/generate_ordinals.ps1
// Do not edit manually.
// Generated from $(($files | Measure-Object).Count) DLLs, $($groups.Count) unique groups.

"@

foreach ($entry in $sortedGroups) {
    $gn = $groupMap[$entry.Key]
    $exports = $entry.Value.exports
    $versions = $entry.Value.versions -join ', '
    $incContent += "// Group $gn ($($entry.Value.versions.Count) versions: $versions)`n"
    $incContent += "static const OrdinalEntry g_ordinals_group${gn}[] = {`n"

    foreach ($ord in ($exports.Keys | Sort-Object)) {
        $name = $exports[$ord]
        if ($name -in $knownFunctions) {
            $incContent += "    OE($name, $ord),`n"
        }
    }
    $incContent += "};`n`n"
}

$incPath = Join-Path $OutDir "ordinals.inc"
$incContent | Out-File $incPath -Encoding UTF8 -NoNewline
Write-Host "Generated $incPath ($groupNum groups)"

# Generate version map JSON
$mapContent = "{`n"
$mapContent += "  `"description`": `"Auto-generated version-to-group mapping`",`n"
$mapContent += "  `"groups`": {`n"

$first = $true
foreach ($entry in $sortedGroups) {
    $gn = $groupMap[$entry.Key]
    if (-not $first) { $mapContent += ",`n" }
    $first = $false
    $verList = ($entry.Value.versions | Sort-Object | ForEach-Object { '"' + $_ + '"' }) -join ', '
    $mapContent += "    `"$gn`": { `"versions`": [ $verList ], `"count`": $($entry.Value.versions.Count) }"
}
$mapContent += "`n  },`n"

$mapContent += "  `"versions`": {`n"
$first = $true
foreach ($v in ($versionMap.Keys | Sort-Object)) {
    if (-not $first) { $mapContent += ",`n" }
    $first = $false
    $mapContent += "    `"$v`": $($versionMap[$v])"
}
$mapContent += "`n  }`n}"

$toolsDir = Join-Path $projectRoot "tools"
$mapPath = Join-Path $toolsDir "ordinals_map.json"
$mapContent | Out-File $mapPath -Encoding UTF8
Write-Host "Generated $mapPath"
Write-Host "Done: $groupNum groups, $(($versionMap.Keys | Measure-Object).Count) versions mapped"
