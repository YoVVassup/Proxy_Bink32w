# Proxy_Bink32w — Bink Video API Proxy DLL

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/)
![Platform](https://img.shields.io/badge/Platform-Windows%20(x86)-blue)
![C++](https://img.shields.io/badge/C%2B%2B-17-green)
![Tests](https://img.shields.io/badge/Tests-438%20passed-brightgreen)
![Bink](https://img.shields.io/badge/Bink-67%20versions-orange)

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

A drop-in `binkw32.dll` proxy that intercepts Bink video API calls between an application and the real Bink DLL. Loads the real DLL by ordinal and forwards all function calls transparently.

Originally developed to enable async media player integration in **Command & Conquer: Red Alert 2 Yuri's Revenge** (and mods), but works with any application that uses the Bink video SDK.

## How it works

1. The application loads `binkw32.dll` (our proxy) from its working directory
2. On first BinkOpen call, the proxy resolves the game executable path and loads the real Bink DLL from the same directory (deferred init to avoid loader lock deadlock)
3. All Bink API functions are resolved **by ordinal** from the real DLL
4. The application calls our exported stubs, which forward directly to the real DLL via `__stdcall` function pointers

```
gamemd.exe → binkw32.dll (proxy) → binkw32_1.0q.dll (real Bink SDK)
```

## ⚙️ Requirements

- MSVC (Visual Studio 2022 or newer; the build examples use the `Visual Studio 18 2026` generator — substitute the generator of your installed VS)
- CMake 3.28+

No Visual C++ Redistributable is required at runtime: the proxy and the test executable link the **static** runtime (`/MT`, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`), so `binkw32.dll` depends only on `KERNEL32.dll`/`WINMM.dll`.

## 🏗️ Building

```bash
# Build default groups (5 = RA2/RA2YR default, 7 = best video quality)
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# Build all 19 groups (each group has its own GROUP_N/ output dir, so parallel is safe)
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

438 tests across 50 test suites covering all core modules:

| Module | Tests | Coverage |
|--------|-------|----------|
| config.cpp (CRC32, .mix parser, .bik header, .wav decoder, config parser, base dir) | 103 | 100% |
| binkw32_proxy.cpp (TrackVideo, UntrackVideo, FindVideo, scaling, DLL lifecycle, ExtractFileName, BINKIOPROCESSOR, CCFileClass, BinkSetPan, BinkSetWillLoop, BinkWait, ReadU32/ReadU16, BppFromFlags, SetSoundTrack/YUV arity adapters) | 152 | 100% |
| wav_player.cpp (alloc, free, start, stop, pause, resume, seek) | 55 | 100% |
| logging.cpp (Log, LogF, TrimRight) | 18 | 100% |
| audio_decoder.cpp (WAV, OGG, negative tests) | 24 | 100% |
| mix_crypto.cpp (Blowfish key derivation, encrypted .mix) | 17 | 100% |
| Corrupt data (malformed .mix, .bik, .wav, config) | 35 | — |
| Integration (DLL exports, ordinals, real files, WAV decode, proxy pipeline) | 19 | — |
| Third-party (OGG, WAV, cross-format, .mix) | 15 | — |

## 📦 Installation

1. Copy the built `GROUP_N/` folder to your game directory
2. Rename `binkw32.dll` inside to replace the game's original
3. Launch the game

If the real DLL is missing, no dialog appears: the failure is written to `binkw32_proxy.log` and every Bink call returns NULL/failure.

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
| 10 | 1.2i-1.5a (2) | BinkDX8SurfaceType, RADSetMemory | Early-mid |
| 11 | 1.1b-1.2a (2) | BinkDX8SurfaceType, RADSetMemory | Early 1.x |
| 12 | 1.2c-1.2d (2) | BinkSetMixBins | — |
| 13 | 1.1c (1) | BinkDX8SurfaceType, RADTimerRead | — |
| 14 | 1.0k (1) | No BinkSetIO, ExpandBink | — |
| 15 | 1.0m (1) | BinkSetIO, ExpandBink + ExpandBundleSizes | — |
| 16 | 1.0h (1) | YUV_blit generic, ExpandBink | — |
| 17 | 1.0i (1) | YUV_blit generic, ExpandBink, RADTimerRead | — |
| 18 | 1.7d (1) | BinkDX9SurfaceType, 86 ordinals | — |
| 19 | 1.0j (1) | ExpandBink + ExpandBundleSizes | — |

## 🎵 Audio replacement

Replace the audio track of any `.bik` video with a custom `.wav` or `.ogg` file. The proxy automatically detects `.bik` files inside `.mix` archives using LMD (Local Mix Database) CRC32 resolution.

### Supported formats

- WAV: PCM, 8/16 bit, 1000–192000 Hz, 1–2 channels (waveOut limit)
- OGG: Vorbis, 1000–192000 Hz, 1–2 channels (via stb_vorbis)
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

`binkw32.cfg` is copied to the output directory during build. Edit it to configure audio replacement.

> The snippet below is an **illustration of every available feature**. The shipped `binkw32.cfg` is a minimal, ready-to-use version: three auto base-dir entries (`|BinkWAV\...`), the `[movies01]`/`[movies02]` maps commented out, and an empty `[audio]` fallback.

```ini
[log]
; enabled = false   ; disable all logging (default: true)
; wait = true       ; log frequent calls: BinkWait, RADTimerRead, radmalloc, ... (default: false)

[exception]
; mix name only — use explicit maps in [mix] sections below
0=movies02.mix
; mix name + base dir — auto: base\stem.ogg, then base\stem.wav
1=movies01.mix|BinkWAV\RA2
2=movmd03.mix|BinkWAV\RA2YR

[movies01]
; Explicit map wins over the auto base dir
a01_f00e.bik = custom\a01_f00e.wav

[movies02]
a01_f00e.bik = BinkWAV\a01_f00e.wav
a02_f00e.bik = BinkWAV\a02_f00e.ogg

[audio]
; Global fallback (used when no exception match)
s01_f00e.bik = BinkWAV\s01_f00e.wav
```

### Priority

The `[exception]` section has **higher priority** than `[audio]`. When a video is opened, the proxy first checks if the `.mix` archive name matches an exception entry, then looks for the `.bik` filename within that exception section. If not found, it falls back to the global `[audio]` section.

**Base dir (`mix|base`):** each exception entry may carry its own relative directory after `|`. For a `.bik` without an explicit map, the proxy resolves `base\stem.ogg` first, then `base\stem.wav` (existence-checked relative to the DLL directory). If neither file exists — falls through to `[audio]`. Entries without `|` have an empty base dir (auto-resolve skipped). Quick audio pack swap: change only the path after `|`.

Reserved section names (`[audio]`, `[exception]`, `[log]`) cannot be used as `.mix` exception section names.

**Section order matters:** the `[exception]` section must appear **before** any per-mix sections (`[movies01]`, etc.). Mix sections are matched against the exception list at the moment they are parsed — a per-mix section placed before `[exception]` (or never declared there) is ignored, and the log records `WARNING: section [name] ignored - no matching entry in [exception]`.

**`[log]` placement:** `[log]` is applied by a pre-pass over the whole file, so it may sit anywhere — everything logged while the rest of the config is parsed already honours `enabled = false`. Only `enabled` and `wait` are recognised keys (`WARNING: unknown [log] key '...'` otherwise).

### How it works

1. When `BinkOpen` is called, the proxy parses the `.mix` archive header and LMD
2. The CRC32 hash is resolved to the original `.bik` filename
3. The filename is matched against `[exception]` (by `.mix` name: explicit map, then base dir) first, then `[audio]`
4. If a mapping exists, the audio file (`.wav` or `.ogg`) is decoded to PCM and played via WaveOut
5. Bink audio is automatically muted (`BinkSetVolume` → 0) for the replaced video
6. The playback stops when `BinkClose` is called

### BINKIOPROCESSOR / CCFileClass support

When the game uses `BINKIOPROCESSOR` (0x02000000) flag with `BinkOpen`, the first parameter is a `CCFileClass*` pointer instead of a filename or file handle. This is used by IHCore and similar mods to read `.bik` files from zip archives.

The proxy automatically extracts the `.bik` filename from the `CCFileClass` using two approaches:
1. **Vtable**: calls `GetFileName()` via vtable[1] (FileClass hierarchy from YRpp)
2. **Fallback**: reads the `FileName` field directly at offset 24 (RawFileClass)

Both approaches use SEH protection against invalid memory access. When the filename is extracted, the proxy searches all `[exception]` sections for a matching `.bik` name, then falls back to `[audio]`.

If filename extraction fails (e.g., non-CCFileClass context), audio replacement is disabled but video playback works normally.

## 📁 .mix archive parsing

The proxy parses RA2/YR `.mix` archive format:

- Header: `uint16` signature `0x0000` at offset 0 (non-zero means a legacy/foreign archive — refused), `uint16` flags at offset 2, `uint16` file count at offset 4, `uint32` `body_size` at offset 6 (must fit the file)
- Hash table at offset `0xA` (12 bytes per entry: CRC32 + offset + size); every entry is cross-checked against the file size and entries reaching past EOF are dropped
- LMD file (CRC32 `0x366E051F`) contains CRC32 → filename mappings
- CRC32 is computed with RA2 convention: uppercase + padding to 4-byte alignment
- Encrypted archives (header flag `& 2`): Blowfish-ECB — the key is derived from the 80-byte `key_source` header field (offset 4) via the embedded Westwood RSA public key, producing a 56-byte Blowfish key (`src/mix_crypto.cpp`), the decrypted index is then parsed as usual

## 📐 Video scaling

When `BinkCopyToBuffer` is called with a destination smaller than the video resolution, the proxy automatically scales the frame using **aspect-ratio-preserving fit scaling** (like CSS `object-fit: contain`). The video is centered within the destination buffer; the surrounding area is left unfilled and therefore appears as letterbox/pillarbox bars automatically (typically black) — no explicit fill is performed.

The scaling uses **nearest-neighbor with pre-computed lookup tables** for maximum speed. Source video (e.g. 1400×1080) is rendered at full resolution into a temp buffer, then efficiently copied to the game buffer using a lookup table that maps each destination pixel to its source pixel. DDraw handles the final stretch to screen resolution — a single interpolation step.

## 📝 Logging

The log file `binkw32_proxy.log` is created in the DLL directory. It rotates automatically on startup and whenever it exceeds **10 MB**: `.log` → `.log.1` → … → `.log.9`, the oldest file is deleted.

### Log options

In `binkw32.cfg`:

```ini
[log]
enabled = false   ; disable all logging (default: true)
wait = true       ; log frequent calls: BinkWait, RADTimerRead, radmalloc, ... (default: false)
```

Accepted boolean values: `true` / `1` / `yes` / `on` and `false` / `0` / `no` / `off` (anything else is reported as `WARNING: [key] value ... not recognised` and the default is kept). A `;` or `#` preceded by whitespace starts an inline comment; in paths such as `C:\a;b` it stays part of the value. Missing `binkw32.cfg` is reported once (`Config not found: ...`) and looked up again on the next video open, so a config created later still takes effect.

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
├── .gitignore           # Git ignore rules
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # Audio replacement config
├── Real/                    # Original Bink DLLs (104 files: 67 supported + 37 excluded versions; not covered by project license)
│   ├── binkw32_1.0q.dll
│   ├── binkw32_1.9u.dll
│   └── ...
├── tests/                    # Google Test suite (438 tests, 50 suites)
│   ├── test_proxy_core.cpp  # FindVideo, UntrackVideo, ScaleBufs, TrackVideoSummary
│   ├── test_uncovered.cpp   # TrackVideo, LogCallStack, EnsureInitialized, Scaling, SoundTrack/YUV arity adapters, sBinkClose, sBinkPause, sBinkGoto, sBinkSetVolume2, sBinkSetSoundOnOff, sBinkSetPan, sBinkSetMixBins, sBinkSetWillLoop, sBinkWait, sBinkDoFrame, BinkOpenWithOptions, ExtractFileName
│   ├── test_binkioprocessor.cpp # BINKIOPROCESSOR flag handling, ExtractNameFromCCFileClass
│   ├── test_corrupt_data.cpp # Negative tests for malformed .mix, .bik, .wav, config
│   ├── test_config_parser.cpp
│   ├── test_audio_decoder.cpp
│   ├── test_wav_player.cpp
│   ├── test_bink_container.cpp
│   ├── test_mix_crc32.cpp
│   ├── test_mix_blowfish.cpp # Encrypted .mix (Blowfish), key derivation
│   ├── test_logging.cpp
│   ├── test_integration.cpp
│   ├── test_third_party.cpp
│   └── ...
├── third-party/             # Real game data used by integration tests (not covered by project license)
├── tools/
│   ├── setup.ps1                # Auto-download dumpbin and ffmpeg from GitHub
│   ├── generate_ordinals.ps1    # Auto-gen ordinal tables from DLLs
│   ├── convert_wav_to_ogg.ps1   # Batch WAV → OGG Vorbis conversion
│   ├── test_bink_minimal.cpp    # Minimal Bink open/frame/close harness
│   ├── test_bink_player.cpp     # Interactive Bink player harness
│   └── ordinals_map.json        # Version→group mapping
└── src/
    ├── binkw32_proxy.h      # Shared types, globals, function declarations
    ├── binkw32_proxy.cpp    # DLL loader, video tracking, proxy exports
    ├── logging.cpp          # Log subsystem
    ├── config.cpp           # Config parsing, .mix parser, Bink header reader
    ├── mix_crypto.{h,cpp}   # Blowfish decryption for encrypted .mix (flags & 2)
    ├── mix_blowfish_s0..s3.inl # Blowfish S-boxes (from ReSource)
    ├── audio_decoder.h      # DecodedAudio struct, DecodeAudioFile declaration
    ├── audio_decoder.cpp    # Unified WAV + OGG decoder (stb_vorbis)
    ├── stb_vorbis.c         # OGG Vorbis decoder (stb_vorbis v1.22, public domain)
    ├── wav_player.cpp       # WaveOut audio playback
    ├── ordinals.inc         # Auto-generated ordinal tables (19 groups)
    ├── exports.def          # DLL export table (111 exports)
    └── version_info.rc      # DLL version info
```

## 🔗 Related projects

**Game / MIX references (used to build this proxy):**

- [Ritanlisa/RA2YR_ReSource](https://github.com/Ritanlisa/RA2YR_ReSource) — decompiled RA2YR source; the source of truth for MIX/CRC/Bink internals
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — MIX file unprotector with LMD recovery
- [secsome/CCFileSystem](https://github.com/secsome/CCFileSystem) — C# MIX parser (legacy/extended/Blowfish, CRC32)
- [Phobos-developers/YRpp](https://github.com/Phobos-developers/YRpp) — C++ interfaces to game classes (`MixFileClass`, CRC engine)
- [Everything-Compatible/YRDict](https://github.com/Everything-Compatible/YRDict) — WW engine implementations, companion to YRpp
- [SethGekco/YR-Hook-Encyclopedia](https://github.com/SethGekco/YR-Hook-Encyclopedia) — registry of 3700+ game addresses and hook frameworks

**binkw32 proxies / loaders (ideas and cross-checks):**

- [Daodan DLL](https://wiki.oni2.net/Daodan_DLL) — Oni mod-carrier built on a binkw32 hijack (config sections, CLI overrides)
- [Erik-JS/masseffect-binkw32](https://github.com/Erik-JS/masseffect-binkw32) — binkw32 proxy with ASI-loader plugin support
- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Bink video mod for Command & Conquer (subtitles/overlay)
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — binkw32 proxy for game modding
- [ThirteenAG/Ultimate-ASI-Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) — de-facto standard ASI proxy loader (can also occupy binkw32 — do not install together with this proxy)
- [elishacloud/dxwrapper](https://github.com/elishacloud/dxwrapper) — wrapper/stub-DLL framework with rich INI config

## ⚠️ Known limitations

- Export names are fixed to the list in `exports.def` (111 total). The volume/pan/mix-bins APIs are exported in **both** arities — `_BinkSetVolume@8` and `@12`, `_BinkSetPan@8` and `@12`, `_BinkSetMixBins@8` and `@16` — so a game importing either signature loads the proxy, and the internal stub adapts the arity to whichever real DLL the build is bound to. Any decoration outside `exports.def` still fails to load the proxy.
- Import by ordinal is not supported — the proxy's own ordinals are pinned in `exports.def` and deliberately differ from the original DLLs (real `binkw32_1.0q.dll` exports `_BinkOpen@8` at ordinal 34, the proxy exports it at `@4`); the generated `ordinals.inc` tables are only used *inside* the proxy to resolve functions in the real DLL. Games must import by name (RA2/RA2YR do).
- Three internal exports that only Bink **1.0h/1.0i** carry are intentionally left out of the generated tables: `_ExpandPlane@44`, `_YUV_blit@56`, `_YUV_blit_mask@56` (1.0j dropped them again). They are blit/decode helpers no game imports, so `tools/generate_ordinals.ps1` does not list them in `$knownFunctions`. Adding them would push `exports.def` from 111 to 114 exports and force a rebuild of all 19 groups.
- Up to 8 simultaneous WAV/OGG replacements (`MAX_WAV_PLAYERS`; the slot is freed on `BinkClose`, so sequential playback is not limited). Replacement audio starts on the **first `BinkDoFrame` call** (or `BinkDoFrameAsync`, which the proxy treats as a frame call), not at `BinkOpen`. If all slots are busy at that moment, the replacement is not started (logged `Failed to start WAV playback`) and the video stays silent — original Bink audio is still muted (mute keys off the queued path, not the player).
- `binkw32.cfg` capacity: up to **64** entries in `[exception]` (`MAX_EXCEPTION_MIXES`), up to **256** explicit maps per `[mix]` section (`MAX_MAPS_PER_MIX`), up to **256** entries in `[audio]` (`MAX_AUDIO_MAPS`). Overflow lines are dropped with a `WARNING: ... limit reached` entry in the log. The auto base dir (`mix|base`) does **not** consume map slots — it resolves on demand and is bounded only by the number of `.bik` files in the archive.
- Other fixed caps: **8** slots in the parsed `.mix` cache (fixed size, no eviction — once full, new `.mix` archives are not parsed and those videos keep their original audio, logged `MixArchive cache full`), **32** concurrently tracked videos (`MAX_TRACKED`), **65535** entries per `.mix` archive (`MIX_MAX_FILES`, u16 format limit). Paths (`mixName`, `baseDir`, `wavPath`) are silently truncated at `MAX_PATH` (260), config lines at 1023 characters, section names at 63 characters.

## 📜 License

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

### Third-party binaries (`Real/`)

The Bink DLLs under [`Real/`](Real/) (and their copies in the build `GROUP_*` directories) are proprietary binaries of RAD Game Tools / their respective rightsholders, included **unmodified and without granting any rights** — the project license above **does not apply to them**. They are kept in the repository solely for interoperability, testing and reference, and must not be deleted.

Author: **YoWassup**
