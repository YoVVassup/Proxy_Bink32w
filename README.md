# Proxy_Bink32w — Bink Video API Proxy DLL

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4./)
![Platform](https://img.shields.io/badge/Platform-Windows%20(x86)-blue)
![C++](https://img.shields.io/badge/C%2B%2B-17-green)
![Tests](https://img.shields.io/badge/Tests-255%20passed-brightgreen)
![Bink](https://img.shields.io/badge/Bink-67%20versions-orange)

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

A drop-in `binkw32.dll` proxy that intercepts Bink video API calls between an application and the real Bink DLL. Loads the real DLL by ordinal and forwards all function calls transparently.

Originally developed to enable async media player integration in **Command & Conquer: Red Alert 2 Yuri's Revenge** (and mod's), but works with any application that uses the Bink video SDK.

## How it works

1. The application loads `binkw32.dll` (our proxy) from its working directory
2. On first BinkOpen call, the proxy resolves the game executable path and loads the real Bink DLL from the same directory (deferred init to avoid loader lock deadlock)
3. All Bink API functions are resolved **by ordinal** from the real DLL
4. The application calls our exported stubs, which forward directly to the real DLL via `__stdcall` function pointers

```
gamemd.exe → binkw32.dll (proxy) → binkw32_1.0q.dll (real Bink SDK)
```

## ⚙️ Requirements

- MSVC (Visual Studio 2022 or newer)
- CMake 3.28+

## 🏗️ Building

```bash
# Build default groups (5 = RA2/RA2YR default, 7 = best video quality)
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# Build all 19 groups (sequential — parallel builds cause linker race on binkw32.exp)
cmake -B build -DBINK_GROUPS="all" -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# Build specific groups
cmake -B build -DBINK_GROUPS="5;7;18" -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

Each group outputs to `build/GROUP_N/Release/` with:
- `binkw32.dll` — the proxy
- `binkw32_X.Yz.dll` — the real Bink DLL (copied from `Real/`)
- `binkw32.cfg` — default config (copied from project root)

## 🧪 Building tests

```bash
cmake -B build_tests -DBUILD_TESTS=ON -G "Visual Studio 18 2026" -A Win32
cmake --build build_tests --config Release
```

### ▶️ Running tests

```bash
# Run all tests
build_tests\tests\Release\bink32w_tests.exe

# Run specific test suite
build_tests\tests\Release\bink32w_tests.exe --gtest_filter="ScalingTest.*"

# Run with game directory for integration tests
set GAME_DIR=C:\path\to\game
build_tests\tests\Release\bink32w_tests.exe
```

### 📈 Test coverage

255 tests across 31 test suites covering all core modules:

| Module | Tests | Coverage |
|--------|-------|----------|
| config.cpp (CRC32, .mix parser, .bik header, .wav decoder, config parser) | 78 | 100% |
| binkw32_proxy.cpp (TrackVideo, UntrackVideo, FindVideo, scaling, DLL lifecycle, ExtractFileName) | 47 | 100% |
| wav_player.cpp (alloc, free, start, stop, pause, resume, seek) | 35 | 100% |
| logging.cpp (Log, LogF, TrimRight, file rotation) | 1 | 100% |
| audio_decoder.cpp (WAV, OGG, negative tests) | 21 | 100% |
| Corrupt data (malformed .mix, .bik, .wav, config) | 26 | — |
| Integration (DLL exports, ordinals, real files) | 13 | — |
| Third-party (OGG, WAV, cross-format, .mix) | 15 | — |

## 📦 Installation

1. Copy the built `GROUP_N/` folder to your game directory
2. Rename `binkw32.dll` inside to replace the game's original
3. Launch the game

If the real DLL is missing, a dialog with an error message will appear.

## 🎮 Bink version compatibility

### Supported versions (19 groups, 67 versions)

| Group | Versions | Status | Example Games |
|-------|----------|--------|---------------|
| 1 | 1.8c-1.8x (12) | ✅ | Dragon Age Origins, Mass Effect, BioShock, COD MW2/MW3 |
| 2 | 1.5e-1.5v (10) | ✅ | Beyond Good and Evil, XIII, FarCry, Divine Divinity |
| 3 | 1.5x-1.7b (9) | ✅ | Psychonauts, Evil Genius, RACE On, Kane & Lynch 2 |
| 4 | 1.9a-1.9h (5) | ✅ | PAYDAY The Heist, Mass Effect 2, Tropico 3, The Witcher |
| **5** | **1.0n-1.0t (5)** | **✅** | **RA2 / RA2YR default** |
| 6 | 1.9i-1.9p (5) | ✅ | Batman Arkham Asylum, Sleeping Dogs, Dishonored, Borderlands |
| **7** | **1.9q-1.9u (3)** | **✅** | **Best video quality** — Portal 2, Just Cause 2, Brink, Duke Nukem Forever |
| 8 | 1.0v-1.0x (3) | ✅ | Fallout Tactics, Red Faction, XCOM Enforcer |
| 9 | 1.8a-1.8b (2) | ✅ | Just Cause |
| 10 | 1.2i-1.5a (2) | ✅ | Morrowind, Syberia |
| 11 | 1.1b-1.2a (2) | ✅ | — |
| 12 | 1.2c-1.2d (2) | ✅ | — |
| 13 | 1.1c (1) | ✅ | — |
| 14 | 1.0k (1) | ✅ | — |
| 15 | 1.0m (1) | ✅ | — |
| 16 | 1.0h (1) | ✅ | — |
| 17 | 1.0i (1) | ✅ | Vampire: The Masquerade - Redemption |
| 18 | 1.7d (1) | ✅ | Advent Rising, Fallout NV, Oblivion |
| 19 | 1.0j (1) | ✅ | Carmageddon TDR2K |

### Excluded versions (37 versions)

| Versions | Reason |
|----------|--------|
| 0.5a-0.9n | Too old, crashes internally (ntdll access violation) |
| 1.0c-1.0f | BinkOpen returns NULL (can't open RA2YR video files) |
| 1.2h | Crashes after BinkSetSoundSystem |
| 1.8r | BinkMake/BinkMix tool, not video API |
| 1.99a-1.99w, 1.9y-1.9z, 2.1c | Pre-release builds, crash after BinkOpen |
| 2.4i, 2.7g | Bink 2.x, different internal implementation |

### Compatible group details

| Group | Versions | Ordinals | Notes |
|-------|----------|----------|-------|
| 1 | 1.8c-1.8x (12) | BinkControlBackgroundIO | Early DX9 |
| 2 | 1.5e-1.5v (10) | BinkCopyToBufferRect, BinkDX8SurfaceType | Mid-2000s |
| 3 | 1.5x-1.7b (9) | BinkSetMemory, YUV blits | Transitional |
| 4 | 1.9a-1.9h (5) | BinkDoFrameAsync, BinkShouldSkip | Pre-1.9u |
| **5** | **1.0n-1.0t (5)** | **83 ordinals, ExpandBink, RADSetMemory** | **RA2/RA2YR default** |
| 6 | 1.9i-1.9p (5) | BinkDoFramePlane, BinkSetMemory | Mid 1.9x |
| **7** | **1.9q-1.9u (3)** | **73 ordinals, BinkSetMemory** | **Best video quality** |
| 8 | 1.0v-1.0x (3) | RADSetMemory, no ExpandBink | Late 1.0x |
| 9 | 1.8a-1.8b (2) | BinkControlBackgroundIO, BinkShouldSkip | Early DX9 |
| 10 | 1.2i-1.5a (2) | BinkDX8SurfaceType, BinkSetMemory | Early-mid |
| 11 | 1.1b-1.2a (2) | BinkDX8SurfaceType, RADSetMemory | Early 1.x |
| 12 | 1.2c-1.2d (2) | BinkSetMixBins | — |
| 13 | 1.1c (1) | BinkDX8SurfaceType, RADTimerRead | — |
| 14 | 1.0k (1) | No BinkSetIO, ExpandBink | — |
| 15 | 1.0m (1) | ExpandBink + ExpandBundleSizes | — |
| 16 | 1.0h (1) | YUV_blit generic, ExpandBink | — |
| 17 | 1.0i (1) | YUV_blit generic, ExpandBink, RADTimerRead | — |
| 18 | 1.7d (1) | BinkDX9SurfaceType, 86 ordinals | — |
| 19 | 1.0j (1) | No ExpandBink, ExpandBundleSizes only | — |

## 🎵 Audio replacement

Replace the audio track of any `.bik` video with a custom `.wav` or `.ogg` file. The proxy automatically detects `.bik` files inside `.mix` archives using LMD (Local Mix Database) CRC32 resolution.

### Supported formats

- WAV: PCM, 8/16 bit, any sample rate, mono/stereo (max 8 channels)
- OGG: Vorbis, any sample rate, mono/stereo (via stb_vorbis)
- Relative paths (from DLL directory) and absolute paths

### Converting WAV to OGG

Use `tools/convert_wav_to_ogg.ps1` to batch-convert WAV files to OGG Vorbis. The script recursively scans all subfolders.

```powershell
# Convert all WAVs in current directory (recursive)
.\tools\convert_wav_to_ogg.ps1

# Convert specific folder
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav\files"

# Higher quality (0=worst, 10=best, default=3)
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -Quality 5

# Preview what will be converted (dry run)
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -DryRun

# Convert and delete original WAVs
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -DeleteOriginal
```

Output example:
```
Found 1022 WAV files, quality=10
  7wolf\a00_f00e.ogg  41098.5KB -> 10584.9KB (26%)
  7wolf\a01_f00e.ogg  17833.5KB -> 4866.6KB (27%)
  ...
Done: 980 converted, 42 failed
```

### Configuration

`binkw32.cfg` is copied to the output directory during build. Edit it to configure audio replacement:

```ini
[exception]
0=movies01.mix
1=movies02.mix

[movies01]
a01_f00e.bik = BinkWAV\a01_f00e.wav
a02_f00e.bik = BinkWAV\a02_f00e.ogg

[movies02]
s01_f00e.bik = BinkWAV\s01_f00e.wav
s02_f00e.bik = BinkWAV\s02_f00e.ogg

[log]
; enabled = false   ; disable all logging (default: true)
; wait = true       ; log BinkWait calls (default: false)

[audio]
; Global fallback (used when no exception match)
s01_f00e.bik = BinkWAV\s01_f00e.wav
```

### Priority

The `[exception]` section has **higher priority** than `[audio]`. When a video is opened, the proxy first checks if the `.mix` archive name matches an exception entry, then looks for the `.bik` filename within that exception section. If not found, it falls back to the global `[audio]` section.

Reserved section names (`[audio]`, `[exception]`, `[log]`) cannot be used as `.mix` exception section names.

### How it works

1. When `BinkOpen` is called, the proxy parses the `.mix` archive header and LMD
2. The CRC32 hash is resolved to the original `.bik` filename
3. The filename is matched against `[exception]` (by `.mix` name) first, then `[audio]`
4. If a mapping exists, the audio file (`.wav` or `.ogg`) is decoded to PCM and played via WaveOut
5. Bink audio is automatically muted (`BinkSetVolume` → 0) for the replaced video
6. The playback stops when `BinkClose` is called

## 📁 .mix archive parsing

The proxy parses RA2/YR `.mix` archive format:

- Header: 4 bytes reserved + `uint16` file count at offset 4
- Hash table at offset `0xA` (12 bytes per entry: CRC32 + offset + size)
- LMD file (CRC32 `0x366E051F`) contains CRC32 → filename mappings
- CRC32 is computed with RA2 convention: uppercase + padding to 4-byte alignment

## 📐 Video scaling

When `BinkCopyToBuffer` is called with a destination smaller than the video resolution, the proxy automatically scales the frame using **aspect-ratio-preserving fit scaling** (like CSS `object-fit: contain`). The video is centered within the destination buffer with black bars if needed.

The scaling uses **nearest-neighbor with pre-computed lookup tables** for maximum speed. Source video (e.g. 1400×1080) is rendered at full resolution into a temp buffer, then efficiently copied to the game buffer using a lookup table that maps each destination pixel to its source pixel. DDraw handles the final stretch to screen resolution — a single interpolation step.

## 📝 Logging

The log file `binkw32_proxy.log` is created in the DLL directory.

### Log options

In `binkw32.cfg`:

```ini
[log]
enabled = false   ; disable all logging (default: true)
wait = true       ; log BinkWait calls (default: false)
```

## 🔄 @N parameter adapters

Some Bink versions have different function signatures for the same API (e.g., `BinkSetVolume@8` vs `@12`). The proxy includes wrapper stubs that adapt between the game's import signature and the real DLL's signature.

## 📊 Call stack logging

When `BinkOpen` is called with a file handle, the proxy logs the call stack with module + RVA information, helping identify which part of the game code initiated the video playback.

## 🔧 Tool setup

Required tools (`dumpbin.exe`, `ffmpeg.exe`) are **automatically downloaded** from GitHub on first use:

```powershell
# Download all tools (dumpbin + ffmpeg)
powershell -ExecutionPolicy Bypass -File tools\setup.ps1

# Download only dumpbin
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Tools dumpbin

# Download only ffmpeg
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Tools ffmpeg

# Force re-download
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Force
```

If tools are not found, `generate_ordinals.ps1` and `convert_wav_to_ogg.ps1` will automatically download them.

## 🔁 Regenerating ordinal tables

To regenerate ordinal tables after adding new Bink DLLs to `Real/`:

```bash
cd Proxy_Bink32w
powershell -ExecutionPolicy Bypass -File tools\generate_ordinals.ps1
```

See `tools/ordinals_map.json` for version→group mapping.

## 📂 Project structure

```
Proxy_Bink32w/
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # Audio replacement config
├── Real/                    # Original Bink DLLs (67 compatible versions)
│   ├── binkw32_1.0q.dll
│   ├── binkw32_1.9u.dll
│   └── ...
├── tests/                   # Google Test suite (255 tests, 31 suites)
│   ├── test_proxy_core.cpp  # TrackVideo, UntrackVideo, FindVideo
│   ├── test_uncovered.cpp   # LogCallStack, EnsureInitialized, Scaling, sBinkClose, ExtractFileName
│   ├── test_corrupt_data.cpp # Negative tests for malformed .mix, .bik, .wav, config
│   ├── test_config_parser.cpp
│   ├── test_audio_decoder.cpp
│   ├── test_wav_player.cpp
│   ├── test_bink_container.cpp
│   ├── test_mix_crc32.cpp
│   ├── test_logging.cpp
│   ├── test_integration.cpp
│   ├── test_third_party.cpp
│   └── ...
├── tools/
│   ├── setup.ps1                # Auto-download dumpbin and ffmpeg from GitHub
│   ├── generate_ordinals.ps1    # Auto-gen ordinal tables from DLLs
│   └── ordinals_map.json        # Version→group mapping
└── src/
    ├── binkw32_proxy.h      # Shared types, globals, function declarations
    ├── binkw32_proxy.cpp    # DLL loader, video tracking, proxy exports
    ├── logging.cpp          # Log subsystem
    ├── config.cpp           # Config parsing, .mix parser, Bink header reader
    ├── audio_decoder.cpp    # Unified WAV + OGG decoder (stb_vorbis)
    ├── stb_vorbis.c         # OGG Vorbis decoder (stb_vorbis v1.22, public domain)
    ├── wav_player.cpp       # WaveOut audio playback
    ├── ordinals.inc         # Auto-generated ordinal tables (19 groups)
    ├── exports.def          # DLL export table (108 exports)
    └── version_info.rc      # DLL version info
```

## 🔗 Related projects

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Bink video mod for Command & Conquer
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — MIX file unprotector with LMD recovery
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — RAD Game Tools libraries
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — Bink proxy for game modding
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — Async media player for Bink 1 and 2

## 📜 License

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

Author: **YoWassup**
