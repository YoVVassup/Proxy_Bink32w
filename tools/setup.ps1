# ============================================================================
# setup.ps1 — Download required tools (dumpbin, ffmpeg) from GitHub releases
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/setup.ps1
#   powershell -ExecutionPolicy Bypass -File tools/setup.ps1 -Tools dumpbin
#   powershell -ExecutionPolicy Bypass -File tools/setup.ps1 -Tools ffmpeg
#   powershell -ExecutionPolicy Bypass -File tools/setup.ps1 -Force  # re-download
# ============================================================================

param(
    [ValidateSet("all", "dumpbin", "ffmpeg")]
    [string]$Tools = "all",
    [switch]$Force
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- dumpbin (Delphier/dumpbin) ---
function Install-Dumpbin {
    $targetDir = Join-Path $scriptDir "dumpbin"
    $targetExe = Join-Path $targetDir "dumpbin.exe"

    if ((Test-Path $targetExe) -and -not $Force) {
        Write-Host "dumpbin.exe already exists at $targetDir" -ForegroundColor Green
        return
    }

    Write-Host "Downloading dumpbin from Delphier/dumpbin..." -ForegroundColor Cyan

    $apiUrl = "https://api.github.com/repos/Delphier/dumpbin/releases/latest"
    try {
        $release = Invoke-RestMethod -Uri $apiUrl -UseBasicParsing
        $asset = $release.assets | Where-Object { $_.name -match "x64\.zip$" } | Select-Object -First 1
        if (-not $asset) {
            Write-Error "No x64 zip found in latest release"
            return
        }
        $downloadUrl = $asset.browser_download_url
        Write-Host "  Version: $($release.tag_name) ($([math]::Round($asset.size / 1MB, 1)) MB)"
    } catch {
        Write-Error "Failed to query GitHub API: $_"
        return
    }

    $zipPath = Join-Path $env:TEMP "dumpbin.zip"
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing
    } catch {
        Write-Error "Failed to download: $_"
        return
    }

    if (Test-Path $targetDir) { Remove-Item $targetDir -Recurse -Force }
    Expand-Archive -Path $zipPath -DestinationPath $targetDir -Force
    Remove-Item $zipPath -Force

    if (Test-Path $targetExe) {
        Write-Host "  Installed to $targetDir" -ForegroundColor Green
    } else {
        # Some zips extract into a subdirectory
        $nested = Get-ChildItem $targetDir -Filter "dumpbin.exe" -Recurse | Select-Object -First 1
        if ($nested) {
            $nestedDir = $nested.DirectoryName
            if ($nestedDir -ne $targetDir) {
                Get-ChildItem $nestedDir | Move-Item -Destination $targetDir -Force
                Remove-Item $nestedDir -Recurse -Force -ErrorAction SilentlyContinue
            }
            Write-Host "  Installed to $targetDir" -ForegroundColor Green
        } else {
            Write-Error "dumpbin.exe not found after extraction"
        }
    }
}

# --- ffmpeg (BtbN/FFmpeg-Builds) ---
function Install-FFmpeg {
    $targetExe = Join-Path $scriptDir "ffmpeg.exe"

    if ((Test-Path $targetExe) -and -not $Force) {
        Write-Host "ffmpeg.exe already exists at $scriptDir" -ForegroundColor Green
        return
    }

    Write-Host "Downloading ffmpeg from BtbN/FFmpeg-Builds..." -ForegroundColor Cyan

    $zipName = "ffmpeg-master-latest-win64-gpl.zip"
    $downloadUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/$zipName"

    $zipPath = Join-Path $env:TEMP "ffmpeg.zip"
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Write-Host "  Downloading $zipName (~160 MB)..."
        Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing
    } catch {
        Write-Error "Failed to download: $_"
        return
    }

    Write-Host "  Extracting..."
    $extractDir = Join-Path $env:TEMP "ffmpeg_extract"
    if (Test-Path $extractDir) { Remove-Item $extractDir -Recurse -Force }
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force
    Remove-Item $zipPath -Force

    # Find ffmpeg.exe in extracted files (inside bin/ subdirectory)
    $found = Get-ChildItem $extractDir -Filter "ffmpeg.exe" -Recurse | Select-Object -First 1
    if ($found) {
        Copy-Item $found.FullName $targetExe -Force
        Remove-Item $extractDir -Recurse -Force
        Write-Host "  Installed to $targetExe" -ForegroundColor Green
    } else {
        Remove-Item $extractDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Error "ffmpeg.exe not found after extraction"
    }
}

# --- Main ---
Write-Host "Proxy_Bink32w — Tool Setup" -ForegroundColor Yellow
Write-Host ""

if ($Tools -eq "all" -or $Tools -eq "dumpbin") { Install-Dumpbin }
if ($Tools -eq "all" -or $Tools -eq "ffmpeg") { Install-FFmpeg }

Write-Host ""
Write-Host "Done." -ForegroundColor Yellow
