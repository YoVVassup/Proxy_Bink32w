# Proxy_Bink32w — Bink Video API Proxy DLL

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

一個可直接替換的 `binkw32.dll` 代理，攔截應用程式與真實 Bink DLL 之間的 Bink 視訊 API 呼叫。透過 ordinal 載入真實 DLL 並透明地轉發所有函式呼叫。

最初開發用於在 **Command & Conquer: Red Alert 2 Yuri's Revenge**（Mental Omega 模組）中整合非同步媒體播放器，但適用於任何使用 Bink 視訊 SDK 的應用程式。

## 運作原理

1. 應用程式從工作目錄載入 `binkw32.dll`（我們的代理）
2. `DllMain` 解析遊戲執行檔路徑，從同一目錄載入真實的 Bink DLL
3. 所有 Bink API 函式透過 **ordinal** 從真實 DLL 解析
4. 應用程式呼叫我們匯出的存根，透過 `__stdcall` 函式指標直接轉發到真實 DLL

## 環境需求

- MSVC（Visual Studio 2022 或更新版本）
- CMake 3.28+

## 編譯

```bash
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

## 音訊替換

將任何 `.bik` 視訊的音軌替換為自訂 `.wav` 檔案。代理透過 LMD（Local Mix Database）CRC32 解析自動偵測 `.mix` 壓縮檔中的 `.bik` 檔案。

### 設定

在 DLL 目錄建立 `binkw32.cfg`：

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
4. 如果找到對應，載入 `.wav` 檔案並透過 WaveOut 播放
5. 替換視訊的 Bink 音訊自動靜音
6. `BinkClose` 時停止 `.wav` 播放

### 支援格式

- WAV 檔案：PCM，8/16/24 位元，任意取樣率，單聲道/立體聲
- 相對路徑（從 DLL 目錄）和絕對路徑

## .mix 壓縮檔解析

代理解析 RA2/YR `.mix` 壓縮檔格式：

- 標頭：4 位元組保留 + offset 4 處的 `uint16` 檔案數量
- offset `0xA` 處的雜湊表（每筆記錄 12 位元組：CRC32 + offset + size）
- LMD 檔案（CRC32 `0x366E051F`）包含 CRC32 → 檔案名對應
- CRC32 按 RA2 規範計算：大寫 + 填充到 4 位元組對齊

## 視訊縮放

當 `BinkCopyToBuffer` 的目標緩衝區小於視訊解析度時，代理使用**保持寬高比的適配縮放**（類似 CSS `object-fit: contain`）自動縮放影格。視訊在目標緩衝區中置中，必要時加上黑邊。

## 日誌記錄

日誌檔案 `binkw32_proxy.log` 建立在 DLL 目錄。

### 停用日誌

1. **檔案** — 在 DLL 目錄中建立空的 `binkw32.nolog` 檔案
2. **環境變數** — 設定 `BINK_PROXY_LOG=0`

### 呼叫堆疊日誌

呼叫 `BinkOpen` 時，代理記錄包含模組和 RVA 資訊的呼叫堆疊，協助識別遊戲程式碼中哪部分發起了視訊播放。

## 專案結構

```
Proxy_Bink32w/
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # 音訊替換設定
└── src/
    ├── binkw32_proxy.cpp    # 主代理 + 音訊 + .mix 解析器
    ├── exports.def          # DLL 匯出表（107 個匯出）
    └── version_info.rc      # DLL 版本資訊
```

## 相關專案

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Command & Conquer 的 Bink 視訊模組
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — MIX 檔案解保護工具，支援 LMD 恢復
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — RAD Game Tools 函式庫
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — 用於遊戲模組的 Bink 代理
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — Bink 1 和 2 的非同步媒體播放器

## 授權條款

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons 姓名標示-非商業性-相同方式分享 4.0 國際授權條款

作者：**YoWassup**
