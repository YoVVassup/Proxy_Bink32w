# Proxy_Bink32w — Bink Video API Proxy DLL

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

A drop-in `binkw32.dll` proxy that intercepts Bink video API calls between an application and the real Bink DLL. Loads the real DLL by ordinal and forwards all function calls transparently.

Originally developed to enable async media player integration in **Command & Conquer: Red Alert 2 Yuri's Revenge** (Mental Omega mod), but works with any application that uses the Bink video SDK.

## Video comparison

> ▶ [Original Bink 1.0q](https://disk.yandex.ru/i/akwI_OxaeQaPvg) | ▶ [Custom Bink 1.9u](https://disk.yandex.ru/i/izkeGHOKPlggGw)

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
cmake -B build -G "Visual Studio 18 2022" -A Win32
cmake --build build --config Release
```

Two targets are built:

| Target | Output | Description |
|---|---|---|
| `binkw32_10q` | `build/BINK_10Q/Release/binkw32.dll` | Proxy for Bink 1.0q (83 ordinal exports) |
| `binkw32_19u` | `build/BINK_19U/Release/binkw32.dll` | Proxy for Bink 1.9u (73 ordinal exports) |

## Installation

1. Copy the appropriate `binkw32.dll` from `build/BINK_10Q/` or `build/BINK_19U/` to your game directory
2. Copy the matching real Bink DLL:
   - For 1.0q build: place `binkw32_1.0q.dll` in the game directory
   - For 1.9u build: place `binkw32_1.9u.dll` in the game directory
3. Launch the game

If the real DLL is missing, a dialog with an error message will appear.

## Logging

The log file `binkw32_proxy.log` is created in the DLL directory.

### Disabling logging

Two ways to disable logging without recompiling:

1. **File** — create an empty `binkw32.nolog` file in the DLL directory
2. **Environment variable** — set `BINK_PROXY_LOG=0`

### Custom path

The `BINK_PROXY_LOG` environment variable specifies a custom log file path.

## Compatibility

| Bink Version | Ordinals | Notes |
|---|---|---|
| 1.9u | 73 exports | BinkSetVolume@12, BinkSetPan@12 |
| 1.0q | 83 exports | BinkSetVolume@8, BinkSetPan@8, YUV blit functions |

## @N parameter adapters

Some Bink versions have different function signatures for the same API. The proxy includes wrapper stubs:

**Bink 1.9u:**
- `_BinkSetVolume@8` (2 params, game import) → `_BinkSetVolume@12` (3 params, real DLL) + `0`
- `_BinkSetSoundTrack@4` (1 param) → `_BinkSetSoundTrack@8` (2 params) + `0`

**Bink 1.0q:**
- `_BinkSetPan@12` (3 params, game import) → `_BinkSetPan@8` (2 params, real DLL)

## Project structure

```
Proxy_Bink32w/
├── CMakeLists.txt           # Two build targets: binkw32_10q and binkw32_19u
├── LICENSE                  # CC BY-NC-SA 4.0
├── src/
│   ├── binkw32_proxy.cpp    # Main proxy: DllMain + LoadDll + forwarding stubs
│   ├── exports.def          # DLL export table
│   └── version_info.rc      # DLL version info
└── README.md
```

## Technical details

- **Platform**: Win32 (x86)
- **Calling convention**: All stubs use `extern "C" __stdcall`
- **Exports**: Defined via `.def` file with explicit `@N` decorations
- **Version selection**: Compile-time `#ifdef BINK_10Q` / `BINK_19U` selects ordinal mapping and DLL name
- **No CRT in DllMain**: Uses `CreateFileA`/`WriteFile` for logging
- **Ordinal-only resolution**: `GetProcAddress(h, (LPCSTR)ordinal)` for all Bink functions

## Related projects

These projects inspired the creation of this proxy:

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Bink video mod for Command & Conquer
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — RAD Game Tools libraries
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — Bink proxy for game modding
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — Async media player for Bink 1 and 2

## License

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

Author: **YoWassup**
