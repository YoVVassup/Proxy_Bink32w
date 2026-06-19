# Proxy_Bink32w — Bink Video API Proxy DLL

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

A drop-in `binkw32.dll` proxy that intercepts Bink video API calls between an application and the real Bink DLL. Loads the real DLL by ordinal and forwards all function calls transparently.

Originally developed to enable async media player integration in **Command & Conquer: Red Alert 2 Yuri's Revenge** (Mental Omega mod), but works with any application that uses the Bink video SDK.

## How it works

1. The application loads `binkw32.dll` (our proxy) from its working directory
2. `DllMain` resolves the game executable path and loads the real Bink DLL (`binkw32_1.0q.dll` or `binkw32_1.9u.dll`) from the same directory
3. All Bink API functions are resolved **by ordinal** from the real DLL
4. The application calls our exported stubs, which forward directly to the real DLL via `__stdcall` function pointers

```
gamemd.exe → binkw32.dll (proxy) → binkw32_1.0q.dll (real Bink SDK)
gamemd.exe → binkw32.dll (proxy) → binkw32_1.9u.dll (real Bink SDK)
```

## Requirements

- MSVC (Visual Studio 2022 or newer)
- CMake 3.28+

## Building

```bash
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

Two targets are built:

| Target | Output | Description |
|---|---|---|
| `binkw32_10q` | `build/BINK_10Q/Release/binkw32.dll` | Proxy for Bink 1.0q |
| `binkw32_19u` | `build/BINK_19U/Release/binkw32.dll` | Proxy for Bink 1.9u |

## Installation

1. Copy the appropriate `binkw32.dll` from `build/BINK_10Q/` or `build/BINK_19U/` to your game directory
2. Copy the matching real Bink DLL:
   - For 1.0q build: place `binkw32_1.0q.dll` in the game directory
   - For 1.9u build: place `binkw32_1.9u.dll` in the game directory
3. Launch the game

If the real DLL is missing, a dialog with an error message will appear.

## Audio replacement

Replace the audio track of any `.bik` video with a custom `.wav` file. The proxy automatically detects `.bik` files inside `.mix` archives using LMD (Local Mix Database) CRC32 resolution.

### Configuration

Create `binkw32.cfg` in the DLL directory:

```ini
[exception]
0=movies01.mix
1=movies02.mix

[movies01]
a01_f00e.bik = BinkWAV\a01_f00e.wav
a02_f00e.bik = BinkWAV\a02_f00e.wav

[movies02]
s01_f00e.bik = BinkWAV\s01_f00e.wav
s02_f00e.bik = BinkWAV\s02_f00e.wav

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
4. If a mapping exists, the `.wav` file is loaded and played via WaveOut
5. Bink audio is automatically muted (`BinkSetVolume` → 0) for the replaced video
6. The `.wav` playback stops when `BinkClose` is called

### Supported formats

- WAV files: PCM, 8/16/24 bit, any sample rate, mono/stereo
- Relative paths (from DLL directory) and absolute paths

## .mix archive parsing

The proxy parses RA2/YR `.mix` archive format:

- Header: 4 bytes reserved + `uint16` file count at offset 4
- Hash table at offset `0xA` (12 bytes per entry: CRC32 + offset + size)
- LMD file (CRC32 `0x366E051F`) contains CRC32 → filename mappings
- CRC32 is computed with RA2 convention: uppercase + padding to 4-byte alignment

## Video scaling

When `BinkCopyToBuffer` is called with a destination smaller than the video resolution, the proxy automatically scales the frame using **aspect-ratio-preserving fit scaling** (like CSS `object-fit: contain`). The video is centered within the destination buffer with black bars if needed.

- Bilinear interpolation for 4 bpp (32-bit) surfaces
- Nearest-neighbor for 2/3 bpp surfaces

## Logging

The log file `binkw32_proxy.log` is created in the DLL directory.

### Disabling logging

Two ways to disable logging without recompiling:

1. **File** — create an empty `binkw32.nolog` file in the DLL directory
2. **Environment variable** — set `BINK_PROXY_LOG=0`

### Custom path

The `BINK_PROXY_LOG` environment variable specifies a custom log file path.

### Log options

In `binkw32.cfg`:

```ini
[log]
wait = true    ; log BinkWait calls (default: false)
```

## Compatibility

| Bink Version | Notes |
|---|---|
| 1.9u | BinkSetVolume@12, BinkSetPan@12 |
| 1.0q | BinkSetVolume@8, BinkSetPan@8, YUV blit functions |

The `.def` export table contains all 107 exports for both builds. Unused ordinals resolve to NULL and silently no-op.

## @N parameter adapters

Some Bink versions have different function signatures for the same API. The proxy includes wrapper stubs that adapt between the game's import signature and the real DLL's signature.

## Call stack logging

When `BinkOpen` is called with a file handle, the proxy logs the call stack with module + RVA information, helping identify which part of the game code initiated the video playback.

## Project structure

```
Proxy_Bink32w/
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # Audio replacement config
└── src/
    ├── binkw32_proxy.cpp    # Main proxy + audio + mix parser
    ├── exports.def          # DLL export table (107 exports)
    └── version_info.rc      # DLL version info
```

## Related projects

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Bink video mod for Command & Conquer
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — MIX file unprotector with LMD recovery
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — RAD Game Tools libraries
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — Bink proxy for game modding
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — Async media player for Bink 1 and 2

## License

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

Author: **YoWassup**
