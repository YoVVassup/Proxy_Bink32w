# Proxy_Bink32w — Bink Video API Proxy DLL

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4./)
![Platform](https://img.shields.io/badge/Platform-Windows%20(x86)-blue)
![C++](https://img.shields.io/badge/C%2B%2B-17-green)
![Tests](https://img.shields.io/badge/Tests-295%20passed-brightgreen)
![Bink](https://img.shields.io/badge/Bink-67%20versions-orange)

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

一個可直接替換的 `binkw32.dll` 代理，攔截應用程式與真實 Bink DLL 之間的 Bink 視訊 API 呼叫。透過 ordinal 載入真實 DLL 並透明地轉發所有函式呼叫。

最初開發用於在 **Command & Conquer: Red Alert 2 Yuri's Revenge**（模組）中整合非同步媒體播放器，但適用於任何使用 Bink 視訊 SDK 的應用程式。

## 運作原理

1. 應用程式從工作目錄載入 `binkw32.dll`（我們的代理）
2. 首次呼叫 BinkOpen 時，代理解析遊戲執行檔路徑並從同一目錄載入真實的 Bink DLL（延遲初始化以避免 loader lock 死鎖）
3. 所有 Bink API 函式透過 **ordinal** 從真實 DLL 解析
4. 應用程式呼叫我們匯出的存根，透過 `__stdcall` 函式指標直接轉發到真實 DLL

```
gamemd.exe → binkw32.dll (proxy) → binkw32_1.0q.dll (real Bink SDK)
```

## ⚙️ 環境需求

- MSVC（Visual Studio 2022 或更新版本）
- CMake 3.28+

## 🏗️ 編譯

```bash
# 建構預設群組（5 = RA2/RA2YR 預設，7 = 最佳影片品質）
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# 建構全部 19 組（順序建構 — 並行建構會導致 binkw32.exp 連結器競爭）
cmake -B build -DBINK_GROUPS="all" -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# 建構指定組
cmake -B build -DBINK_GROUPS="5;7;18" -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

每組輸出到 `build/GROUP_N/Release/`，包含：
- `binkw32.dll` — 代理 DLL
- `binkw32_X.Yz.dll` — 真實 Bink DLL（從 `Real/` 複製）
- `binkw32.cfg` — 預設設定（從專案根目錄複製）

## 🧪 建構測試

```bash
cmake -B build_tests -DBUILD_TESTS=ON -G "Visual Studio 18 2026" -A Win32
cmake --build build_tests --config Release
```

### ▶️ 執行測試

```bash
# 執行所有測試
build_tests\tests\Release\bink32w_tests.exe

# 執行指定測試套件
build_tests\tests\Release\bink32w_tests.exe --gtest_filter="ScalingTest.*"

# 使用遊戲目錄執行整合測試
set GAME_DIR=C:\path\to\game
build_tests\tests\Release\bink32w_tests.exe
```

### 📈 測試覆蓋率

295 個測試分佈在 38 個測試套件中，覆蓋所有核心模組：

| 模組 | 測試數 | 覆蓋率 |
|------|--------|--------|
| config.cpp（CRC32、.mix 解析器、.bik 標頭、.wav 解碼器、設定解析器） | 78 | 100% |
| binkw32_proxy.cpp（TrackVideo、UntrackVideo、FindVideo、縮放、DLL 生命週期、ExtractFileName、BINKIOPROCESSOR、CCFileClass） | 78 | 100% |
| wav_player.cpp（分配、釋放、啟動、停止、暫停、恢復、跳轉） | 35 | 100% |
| logging.cpp（Log、LogF、TrimRight） | 13 | 100% |
| audio_decoder.cpp（WAV、OGG、負面測試） | 21 | 100% |
| 損壞資料測試（畸形 .mix、.bik、.wav、設定） | 26 | — |
| 整合測試（DLL 匯出、序數、真實檔案、WAV 解碼） | 14 | — |
| 第三方（OGG、WAV、跨格式、.mix） | 15 | — |

## 📦 安裝

1. 將建構好的 `GROUP_N/` 資料夾複製到遊戲目錄
2. 將其中的 `binkw32.dll` 重新命名以替換遊戲原始檔案
3. 啟動遊戲

如果真實 DLL 缺失，將出現錯誤訊息對話框。

## 🎮 Bink 版本相容性

### 支援的版本（19 組，67 個版本）

| 組 | 版本 | 狀態 | 範例遊戲 |
|----|------|------|----------|
| 1 | 1.8c-1.8x (12) | ✅ | Dragon Age Origins, Mass Effect, BioShock, COD MW2/MW3 |
| 2 | 1.5e-1.5v (10) | ✅ | Beyond Good and Evil, XIII, FarCry, Divine Divinity |
| 3 | 1.5x-1.7b (9) | ✅ | Psychonauts, Evil Genius, RACE On, Kane & Lynch 2 |
| 4 | 1.9a-1.9h (5) | ✅ | PAYDAY The Heist, Mass Effect 2, Tropico 3, The Witcher |
| **5** | **1.0n-1.0t (5)** | **✅** | **RA2 / RA2YR 預設** |
| 6 | 1.9i-1.9p (5) | ✅ | Batman Arkham Asylum, Sleeping Dogs, Dishonored, Borderlands |
| **7** | **1.9q-1.9u (3)** | **✅** | **最佳影片品質** — Portal 2, Just Cause 2, Brink, Duke Nukem Forever |
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

### 排除的版本（37 個版本）

| 版本 | 原因 |
|------|------|
| 0.5a-0.9n | 過於老舊，內部崩潰（ntdll 存取違規） |
| 1.0c-1.0f | BinkOpen 回傳 NULL（無法開啟 RA2YR 視訊檔案） |
| 1.2h | BinkSetSoundSystem 後崩潰 |
| 1.8r | BinkMake/BinkMix 工具，非視訊 API |
| 1.99a-1.99w, 1.9y-1.9z, 2.1c | 預先發布版本，BinkOpen 後崩潰 |
| 2.4i, 2.7g | Bink 2.x，不同內部實作 |

### 相容組詳細資訊

| 組 | 版本 | 序數 | 備註 |
|----|------|------|------|
| 1 | 1.8c-1.8x (12) | BinkControlBackgroundIO | 早期 DX9 |
| 2 | 1.5e-1.5v (10) | BinkCopyToBufferRect, BinkDX8SurfaceType | 2000年代中期 |
| 3 | 1.5x-1.7b (9) | BinkSetMemory, YUV blits | 過渡期 |
| 4 | 1.9a-1.9h (5) | BinkDoFrameAsync, BinkShouldSkip | 1.9u 之前 |
| **5** | **1.0n-1.0t (5)** | **83 序數, ExpandBink, RADSetMemory** | **RA2/RA2YR 預設** |
| 6 | 1.9i-1.9p (5) | BinkDoFramePlane, BinkSetMemory | 1.9x 中期 |
| **7** | **1.9q-1.9u (3)** | **73 序數, BinkSetMemory** | **最佳影片品質** |
| 8 | 1.0v-1.0x (3) | RADSetMemory，無 ExpandBink | 1.0x 後期 |
| 9 | 1.8a-1.8b (2) | BinkControlBackgroundIO, BinkShouldSkip | 早期 DX9 |
| 10 | 1.2i-1.5a (2) | BinkDX8SurfaceType, BinkSetMemory | 早期-中期 |
| 11 | 1.1b-1.2a (2) | BinkDX8SurfaceType, RADSetMemory | 早期 1.x |
| 12 | 1.2c-1.2d (2) | BinkSetMixBins | — |
| 13 | 1.1c (1) | BinkDX8SurfaceType, RADTimerRead | — |
| 14 | 1.0k (1) | 無 BinkSetIO, ExpandBink | — |
| 15 | 1.0m (1) | ExpandBink + ExpandBundleSizes | — |
| 16 | 1.0h (1) | YUV_blit 通用, ExpandBink | — |
| 17 | 1.0i (1) | YUV_blit 通用, ExpandBink, RADTimerRead | — |
| 18 | 1.7d (1) | BinkDX9SurfaceType, 86 序數 | — |
| 19 | 1.0j (1) | 無 ExpandBink，僅 ExpandBundleSizes | — |

## 🎵 音訊替換

將任何 `.bik` 視訊的音軌替換為自訂 `.wav` 或 `.ogg` 檔案。代理透過 LMD（Local Mix Database）CRC32 解析自動偵測 `.mix` 壓縮檔中的 `.bik` 檔案。

### 支援格式

- WAV 檔案：PCM，8/16 位元，任意取樣率，單聲道/立體聲（最多 8 聲道）
- OGG：Vorbis，任意取樣率，單聲道/立體聲（透過 stb_vorbis）
- 相對路徑（從 DLL 目錄）和絕對路徑

### WAV 轉 OGG

使用 `tools/convert_wav_to_ogg.ps1` 批次將 WAV 檔案轉換為 OGG Vorbis。腳本遞迴掃描所有子資料夾。

```powershell
# 轉換目前目錄下所有 WAV（遞迴）
.\tools\convert_wav_to_ogg.ps1

# 轉換指定資料夾
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav\files"

# 更高品質（0=最差, 10=最佳, 預設=3）
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -Quality 5

# 預覽將要轉換的檔案（乾運行）
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -DryRun

# 轉換並刪除原始 WAV
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -DeleteOriginal
```

輸出範例：
```
Found 1022 WAV files, quality=10
  7wolf\a00_f00e.ogg  41098.5KB -> 10584.9KB (26%)
  7wolf\a01_f00e.ogg  17833.5KB -> 4866.6KB (27%)
  ...
Done: 980 converted, 42 failed
```

### 設定

`binkw32.cfg` 在建構期間複製到輸出目錄。編輯它以設定音訊替換：

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
; enabled = false   ; 停用所有日誌記錄（預設：true）
; wait = true       ; 記錄 BinkWait 呼叫（預設：false）

[audio]
; 全域回退（當 exception 中未找到時使用）
s01_f00e.bik = BinkWAV\s01_f00e.wav
```

### 優先順序

`[exception]` 段的優先順序**高於** `[audio]`。代理首先檢查 `.mix` 壓縮檔名是否匹配 exception 條目，然後在該 exception 段中尋找 `.bik` 檔案名。如果未找到，則回退到全域 `[audio]` 段。

保留的段名（`[audio]`、`[exception]`、`[log]`）不能用作 `.mix` exception 段名。

### 運作流程

1. 呼叫 `BinkOpen` 時，代理解析 `.mix` 壓縮檔標頭和 LMD
2. CRC32 雜湊解析為原始 `.bik` 檔案名
3. 檔案名先與 `[exception]`（按 `.mix` 名稱）匹配，再與 `[audio]` 匹配
4. 如果找到對應，載入 `.wav` 或 `.ogg` 檔案並解碼為 PCM 透過 WaveOut 播放
5. 替換視訊的 Bink 音訊自動靜音
6. `BinkClose` 時停止播放

### BINKIOPROCESSOR / CCFileClass 支援

當遊戲使用 `BINKIOPROCESSOR` (0x02000000) 標誌呼叫 `BinkOpen` 時，第一個參數是 `CCFileClass*` 指標而非檔名或檔案控制代碼。IHCore 等模組使用此方式從 zip 壓縮檔中讀取 `.bik` 檔案。

代理自動從 `CCFileClass` 提取 `.bik` 檔名，使用兩種方式：
1. **Vtable**：透過 vtable[1] 呼叫 `GetFileName()`（YRpp 中的 FileClass 層級）
2. **Fallback**：直接讀取 offset 24 處的 `FileName` 欄位（RawFileClass）

兩種方式均使用 SEH 保護防止無效記憶體存取。提取檔名後，代理在所有 `[exception]` 區段中搜尋匹配的 `.bik` 名稱，然後回退到 `[audio]`。

如果檔名提取失敗（例如非 CCFileClass 上下文），音訊替換被停用但影片正常播放。

## 📁 .mix 壓縮檔解析

代理解析 RA2/YR `.mix` 壓縮檔格式：

- 標頭：4 位元組保留 + offset 4 處的 `uint16` 檔案數量
- offset `0xA` 處的雜湊表（每筆記錄 12 位元組：CRC32 + offset + size）
- LMD 檔案（CRC32 `0x366E051F`）包含 CRC32 → 檔案名對應
- CRC32 按 RA2 規範計算：大寫 + 填充到 4 位元組對齊

## 📐 視訊縮放

當 `BinkCopyToBuffer` 的目標緩衝區小於視訊解析度時，代理使用**保持寬高比的適配縮放**（類似 CSS `object-fit: contain`）自動縮放影格。視訊在目標緩衝區中置中，必要時加上黑邊。

縮放使用**帶預計算查找表的最近鄰演算法**以實現最大速度。源視訊以全解析度渲染到暫存緩衝區，然後使用查找表高效地複製到遊戲緩衝區。DDraw 負責最終的螢幕拉伸——單次插值。

## 📝 日誌記錄

日誌檔案 `binkw32_proxy.log` 建立在 DLL 目錄。

### 日誌選項

在 `binkw32.cfg` 中：

```ini
[log]
enabled = false   ; 停用所有日誌記錄（預設：true）
wait = true       ; 記錄 BinkWait 呼叫（預設：false）
```

## 🔄 @N 參數介面卡

某些 Bink 版本對相同 API 有不同的函式簽章（例如 `BinkSetVolume@8` vs `@12`）。代理包含包裝存根，用於在遊戲的匯入簽章與真實 DLL 簽章之間進行介接。

## 📊 呼叫堆疊日誌

當使用檔案控制代碼呼叫 `BinkOpen` 時，代理記錄包含模組和 RVA 資訊的呼叫堆疊，協助識別遊戲程式碼中哪部分發起了視訊播放。

## 🔧 工具設定

所需工具（`dumpbin.exe`、`ffmpeg.exe`）在首次使用時從 GitHub **自動下載**：

```powershell
# 下載所有工具（dumpbin + ffmpeg）
powershell -ExecutionPolicy Bypass -File tools\setup.ps1

# 僅下載 dumpbin
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Tools dumpbin

# 僅下載 ffmpeg
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Tools ffmpeg

# 強制重新下載
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Force
```

如果未找到工具，`generate_ordinals.ps1` 和 `convert_wav_to_ogg.ps1` 將自動下載。

## 🔁 重新生成序數表

在 `Real/` 中新增新的 Bink DLL 後，重新生成序數表：

```bash
cd Proxy_Bink32w
powershell -ExecutionPolicy Bypass -File tools\generate_ordinals.ps1
```

參見 `tools/ordinals_map.json` 了解版本→組對應。

## 📂 專案結構

```
Proxy_Bink32w/
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # 音訊替換設定
├── Real/                    # 原始 Bink DLL（67 個相容版本）
│   ├── binkw32_1.0q.dll
│   ├── binkw32_1.9u.dll
│   └── ...
├── tests/                   # Google Test 套件（295 個測試，38 個測試套件）
│   ├── test_proxy_core.cpp  # TrackVideo、UntrackVideo、FindVideo
│   ├── test_uncovered.cpp   # LogCallStack、EnsureInitialized、Scaling、sBinkClose、sBinkPause、sBinkGoto、sBinkSetVolume2、sBinkSetSoundOnOff、ExtractFileName
│   ├── test_binkioprocessor.cpp # BINKIOPROCESSOR 標誌處理、ExtractNameFromCCFileClass
│   ├── test_corrupt_data.cpp # 損壞 .mix、.bik、.wav、設定的負面測試
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
│   ├── setup.ps1                # 從 GitHub 自動下載 dumpbin 和 ffmpeg
│   ├── generate_ordinals.ps1    # 從 DLL 自動產生序數表
│   └── ordinals_map.json        # 版本→組對應
└── src/
    ├── binkw32_proxy.h      # 共用型別、全域變數、函式宣告
    ├── binkw32_proxy.cpp    # DLL 載入器、視訊追蹤、代理匯出
    ├── logging.cpp          # 日誌子系統
    ├── config.cpp           # 設定解析、.mix 解析器、Bink 標頭讀取
    ├── audio_decoder.cpp    # 統一 WAV + OGG 解碼器（stb_vorbis）
    ├── stb_vorbis.c         # OGG Vorbis 解碼器（stb_vorbis v1.22，公共領域）
    ├── wav_player.cpp       # WaveOut 音訊播放
    ├── ordinals.inc         # 自動產生的序數表（19 組）
    ├── exports.def          # DLL 匯出表（108 個匯出）
    └── version_info.rc      # DLL 版本資訊
```

## 🔗 相關專案

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Command & Conquer 的 Bink 視訊模組
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — MIX 檔案解保護工具，支援 LMD 恢復
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — RAD Game Tools 函式庫
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — 用於遊戲模組的 Bink 代理
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — Bink 1 和 2 的非同步媒體播放器

## 📜 授權條款

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons 姓名標示-非商業性-相同方式分享 4.0 國際授權條款

作者：**YoWassup**
