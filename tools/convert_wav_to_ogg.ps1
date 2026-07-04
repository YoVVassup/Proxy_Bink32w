# ============================================================================
# convert_wav_to_ogg.ps1 — Batch WAV to OGG (Vorbis) converter
#
# Requires: ffmpeg in PATH or ffmpeg.exe in script directory
#
# Usage:
#   .\convert_wav_to_ogg.ps1                          # current dir
#   .\convert_wav_to_ogg.ps1 "C:\path\to\wav\files"  # specific dir
#   .\convert_wav_to_ogg.ps1 -InputDir "C:\path" -Quality 5
#   .\convert_wav_to_ogg.ps1 -InputDir "C:\path" -DryRun
#
# Quality: 0 (worst, ~45kbps) to 10 (best, ~500kbps). Default: 3 (~112kbps)
# ============================================================================

param(
    [string]$InputDir = ".",
    [ValidateRange(0,10)]
    [int]$Quality = 3,
    [switch]$DryRun,
    [switch]$DeleteOriginal
)

function Find-FFmpeg {
    $local = Join-Path $PSScriptRoot "ffmpeg.exe"
    if (Test-Path $local) { return $local }
    $found = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    return $null
}

$ffmpeg = Find-FFmpeg
if (-not $ffmpeg) {
    Write-Host "ffmpeg not found. Downloading from GitHub..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "setup.ps1") -Tools ffmpeg
    $ffmpeg = Find-FFmpeg
}
if (-not $ffmpeg) {
    Write-Error "ffmpeg not found. Install ffmpeg and add to PATH, or run: tools\setup.ps1"
    exit 1
}

$inputPath = Resolve-Path $InputDir -ErrorAction Stop
$wavFiles = Get-ChildItem -LiteralPath $inputPath -Filter "*.wav" -Recurse -File

if ($wavFiles.Count -eq 0) {
    Write-Host "No .wav files found in $inputPath" -ForegroundColor Yellow
    exit 0
}

Write-Host "Found $($wavFiles.Count) WAV files, quality=$Quality" -ForegroundColor Cyan
if ($DryRun) { Write-Host "DRY RUN - no files will be created" -ForegroundColor Magenta }

$converted = 0
$failed = 0

foreach ($wav in $wavFiles) {
    $relativePath = $wav.FullName.Substring($inputPath.Path.Length).TrimStart('\')
    $oggPath = [System.IO.Path]::ChangeExtension($wav.FullName, ".ogg")
    $oggRelative = [System.IO.Path]::ChangeExtension($relativePath, ".ogg")

    if (-not $DryRun) {
        & $ffmpeg -hide_banner -loglevel error -i $wav.FullName -c:a libvorbis -q:a $Quality -y $oggPath 2>&1 | Out-Null
        $exitCode = $LASTEXITCODE
        if ($exitCode -eq 0 -and (Test-Path -LiteralPath $oggPath)) {
            $converted++
            $wavSize = [math]::Round($wav.Length / 1024, 1)
            $oggSize = [math]::Round((Get-Item -LiteralPath $oggPath).Length / 1024, 1)
            $ratio = [math]::Round($oggSize / $wavSize * 100)
            Write-Host ("  $oggRelative  {0}KB -> {1}KB ({2}%)" -f $wavSize, $oggSize, $ratio) -ForegroundColor Green
            if ($DeleteOriginal) { Remove-Item -LiteralPath $wav.FullName -Force }
        } else {
            $failed++
            Write-Host "  FAILED: $relativePath" -ForegroundColor Red
        }
    } else {
        Write-Host "  $relativePath -> $oggRelative" -ForegroundColor Gray
        $converted++
    }
}

Write-Host ("`nDone: {0} converted, {1} failed" -f $converted, $failed) -ForegroundColor Cyan
