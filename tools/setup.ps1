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

# Any failure sets this so the script exits non-zero instead of printing
# "Done." with code 0.
$script:hadError = $false

# Per-run scratch directory under %TEMP%. A fixed path (dumpbin.zip,
# ffmpeg_extract) is writable by anything on the machine between the download
# and the extraction - a classic TOCTOU window.
function New-WorkDir($name) {
    $dir = Join-Path $env:TEMP ("bink32w_setup_" + $name + "_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $dir | Out-Null
    return $dir
}

function Remove-WorkDir($dir) {
    if ($dir) { Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue }
}

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
            $script:hadError = $true
            return
        }
        $downloadUrl = $asset.browser_download_url
        Write-Host "  Version: $($release.tag_name) ($([math]::Round($asset.size / 1MB, 1)) MB)"
    } catch {
        Write-Error "Failed to query GitHub API: $_"
        $script:hadError = $true
        return
    }

    $workDir = New-WorkDir "dumpbin"
    $zipPath = Join-Path $workDir "dumpbin.zip"
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing
    } catch {
        Write-Error "Failed to download: $_"
        $script:hadError = $true
        Remove-WorkDir $workDir
        return
    }

    # The API reports the asset size - catch a truncated/partial write before
    # anything is extracted.
    $zipSize = (Get-Item $zipPath).Length
    if ($zipSize -ne $asset.size) {
        Write-Error "Size mismatch for dumpbin.zip: got $zipSize bytes, expected $($asset.size)"
        $script:hadError = $true
        Remove-WorkDir $workDir
        return
    }

    # Extract into the scratch directory first: tools/dumpbin is replaced only
    # after a complete extraction, never deleted beforehand.
    $staging = Join-Path $workDir "stage"
    try {
        Expand-Archive -Path $zipPath -DestinationPath $staging -Force
    } catch {
        Write-Error "Failed to extract: $_"
        $script:hadError = $true
        Remove-WorkDir $workDir
        return
    }

    $found = Get-ChildItem $staging -Filter "dumpbin.exe" -Recurse | Select-Object -First 1
    if (-not $found) {
        Write-Error "dumpbin.exe not found after extraction"
        $script:hadError = $true
        Remove-WorkDir $workDir
        return
    }

    if (Test-Path $targetDir) { Remove-Item $targetDir -Recurse -Force }
    New-Item -ItemType Directory -Path $targetDir | Out-Null
    Copy-Item -Path (Join-Path $found.DirectoryName "*") -Destination $targetDir -Recurse -Force
    Remove-WorkDir $workDir

    if (Test-Path $targetExe) {
        Write-Host "  Installed to $targetDir" -ForegroundColor Green
    } else {
        Write-Error "dumpbin.exe missing after install into $targetDir"
        $script:hadError = $true
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

    $workDir = New-WorkDir "ffmpeg"
    $zipPath = Join-Path $workDir $zipName
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Write-Host "  Downloading $zipName (~160 MB)..."
        Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing
    } catch {
        Write-Error "Failed to download: $_"
        $script:hadError = $true
        Remove-WorkDir $workDir
        return
    }

    # No hash is published for the "latest" builds, so at least confirm the
    # payload is a zip (PK) before handing it to Expand-Archive.
    $fs = [System.IO.File]::OpenRead($zipPath)
    $sig = New-Object byte[] 2
    [void]$fs.Read($sig, 0, 2)
    $fs.Close()
    if ($sig[0] -ne 0x50 -or $sig[1] -ne 0x4B) {
        Write-Error "Downloaded file is not a zip archive"
        $script:hadError = $true
        Remove-WorkDir $workDir
        return
    }

    Write-Host "  Extracting..."
    $extractDir = Join-Path $workDir "extract"
    try {
        Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force
    } catch {
        Write-Error "Failed to extract: $_"
        $script:hadError = $true
        Remove-WorkDir $workDir
        return
    }

    # Find ffmpeg.exe in extracted files (inside bin/ subdirectory)
    $found = Get-ChildItem $extractDir -Filter "ffmpeg.exe" -Recurse | Select-Object -First 1
    if ($found) {
        Copy-Item $found.FullName $targetExe -Force
        Remove-WorkDir $workDir
        Write-Host "  Installed to $targetExe" -ForegroundColor Green
    } else {
        Remove-WorkDir $workDir
        Write-Error "ffmpeg.exe not found after extraction"
        $script:hadError = $true
    }
}

# --- Main ---
Write-Host "Proxy_Bink32w — Tool Setup" -ForegroundColor Yellow
Write-Host ""

if ($Tools -eq "all" -or $Tools -eq "dumpbin") { Install-Dumpbin }
if ($Tools -eq "all" -or $Tools -eq "ffmpeg") { Install-FFmpeg }

Write-Host ""
if ($script:hadError) {
    Write-Host "Done with errors." -ForegroundColor Red
    exit 1
}
Write-Host "Done." -ForegroundColor Yellow
