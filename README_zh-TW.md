# Proxy_Bink32w — Bink Video API Proxy DLL

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

一個可直接替換的 `binkw32.dll` 代理，攔截應用程式與真實 Bink DLL 之間的 Bink 視訊 API 呼叫。透過 ordinal 載入真實 DLL 並透明地轉發所有函式呼叫。

最初開發用於在 **Command & Conquer: Red Alert 2 Yuri's Revenge**（Mental Omega 模組）中整合非同步媒體播放器，但適用於任何使用 Bink 視訊 SDK 的應用程式。

## 影片比較

> ▶ [原始 Bink 1.0q](https://disk.yandex.ru/i/akwI_OxaeQaPvg) | ▶ [自訂 Bink 1.9u](https://disk.yandex.ru/i/izkeGHOKPlggGw)

## 運作原理

1. 應用程式從工作目錄載入 `binkw32.dll`（我們的代理）
2. `DllMain` 解析遊戲執行檔路徑，從同一目錄載入真實的 Bink DLL（`binkw32_1.0q.dll` 或 `binkw32_1.9u.dll`）
3. 所有 Bink API 函式透過 **ordinal** 從真實 DLL 解析
4. 應用程式呼叫我們匯出的存根，透過 `__stdcall` 函式指標直接轉發到真實 DLL

```
gamemd.exe → binkw32.dll（代理）→ binkw32_1.0q.dll（真實 Bink SDK）
gamemd.exe → binkw32.dll（代理）→ binkw32_1.9u.dll（真實 Bink SDK）
```

## 環境需求

- MSVC（Visual Studio 2022 或更新版本）
- CMake 3.28+

## 編譯

```bash
cmake -B build -G "Visual Studio 18 2022" -A Win32
cmake --build build --config Release
```

兩個編譯目標：

| 目標 | 輸出 | 說明 |
|---|---|---|
| `binkw32_10q` | `build/BINK_10Q/Release/binkw32.dll` | Bink 1.0q 代理（83 個 ordinal 匯出） |
| `binkw32_19u` | `build/BINK_19U/Release/binkw32.dll` | Bink 1.9u 代理（73 個 ordinal 匯出） |

## 安裝

1. 將對應的 `binkw32.dll` 從 `build/BINK_10Q/` 或 `build/BINK_19U/` 複製到遊戲目錄
2. 複製對應的真實 Bink DLL：
   - 1.0q 編譯：將 `binkw32_1.0q.dll` 放入遊戲目錄
   - 1.9u 編譯：將 `binkw32_1.9u.dll` 放入遊戲目錄
3. 啟動遊戲

若缺少真實 DLL，將顯示錯誤對話框。

## 日誌記錄

日誌檔案 `binkw32_proxy.log` 建立在 DLL 所在目錄。

### 停用日誌

兩種方式可在不重新編譯的情況下停用日誌：

1. **檔案** — 在 DLL 目錄中建立空的 `binkw32.nolog` 檔案
2. **環境變數** — 設定 `BINK_PROXY_LOG=0`

### 自訂路徑

環境變數 `BINK_PROXY_LOG` 可指定自訂的日誌檔案路徑。

## 相容性

| Bink 版本 | Ordinals | 備註 |
|---|---|---|
| 1.9u | 73 個匯出 | BinkSetVolume@12, BinkSetPan@12 |
| 1.0q | 83 個匯出 | BinkSetVolume@8, BinkSetPan@8, YUV blit 函式（ordinal 84–107） |

`.def` 匯出表包含兩個建置的所有 107 個匯出。未使用的 ordinal（如 1.9u 中的 YUV 函式）解析為 NULL 並靜默跳過。

## @N 參數適配器

某些 Bink 版本對相同 API 有不同的函式簽章。代理包含包裝存根，在遊戲的匯入簽章和真實 DLL 的簽章之間進行適配：

**Bink 1.9u：**
- `_BinkSetVolume@12`（3 參數，真實 DLL）← `_BinkSetVolume@8`（2 參數，遊戲匯入）+ `0`
- `_BinkSetSoundTrack@8`（2 參數，真實 DLL）← `_BinkSetSoundTrack@4`（1 參數，遊戲匯入）+ `0`
- `_BinkSetPan@12`（3 參數，真實 DLL）← `_BinkSetPan@8`（2 參數，遊戲匯入）+ `0`

**Bink 1.0q：**
- `_BinkSetVolume@8`（2 參數，真實 DLL）← `_BinkSetVolume@12`（3 參數，遊戲匯入）
- `_BinkSetPan@8`（2 參數，真實 DLL）← `_BinkSetPan@12`（3 參數，遊戲匯入）
- `_BinkSetSoundTrack@4`（1 參數，真實 DLL）← `_BinkSetSoundTrack@8`（2 參數，遊戲匯入）

## 專案結構

```
Proxy_Bink32w/
├── CMakeLists.txt           # 兩個編譯目標：binkw32_10q 和 binkw32_19u
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
└── src/
    ├── binkw32_proxy.cpp    # 主代理：DllMain + LoadDll + 轉發存根
    ├── exports.def          # DLL 匯出表（107 個匯出）
    └── version_info.rc      # DLL 版本資訊
```

## 視訊縮放

當 `BinkCopyToBuffer` 的目標緩衝區小於視訊解析度時，代理會自動使用雙線性插值（4 bpp）或最近鄰插值（2/3 bpp）縮放影格。這允許老舊遊戲在不修改程式碼的情況下在較小的播放區域中顯示高解析度視訊。

## 視訊追蹤

代理透過 `BinkGetSummary` 追蹤開啟的視訊控制代碼（最多 32 個）及其尺寸。這使得 `BinkCopyToBuffer` 中的縮放邏輯能夠在渲染前知道來源解析度。

## 技術細節

- **平台**：Win32 (x86)
- **呼叫慣例**：所有存根使用 `extern "C" __stdcall`
- **匯出**：透過 `.def` 檔案定義，帶有明確的 `@N` 裝飾
- **版本選擇**：編譯時 `#ifdef BINK_10Q` / `BINK_19U` 選擇 ordinal 對應和 DLL 名稱
- **DllMain 中不使用 CRT**：使用 `CreateFileA`/`WriteFile` 進行日誌記錄
- **僅 ordinal 解析**：所有 Bink 函式使用 `GetProcAddress(h, (LPCSTR)ordinal)`
- **BinkSetMemory 別名**：作為 `RADSetMemory` 的別名匯出（兩個函式解析為同一個真實 DLL 函式）

## 相關專案

以下專案啟發了本代理的開發：

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Command & Conquer 的 Bink 視訊模組
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — RAD Game Tools 函式庫
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — 用於遊戲模組的 Bink 代理
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — Bink 1 和 2 的非同步媒體播放器

## 授權條款

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons 姓名標示-非商業性-相同方式分享 4.0 國際授權條款

作者：**YoWassup**
