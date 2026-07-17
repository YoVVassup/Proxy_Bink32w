# Proxy_Bink32w — Bink Video API Proxy DLL

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4./)
![Platform](https://img.shields.io/badge/Platform-Windows%20(x86)-blue)
![C++](https://img.shields.io/badge/C%2B%2B-17-green)
![Tests](https://img.shields.io/badge/Tests-306%20passed-brightgreen)
![Bink](https://img.shields.io/badge/Bink-67%20versions-orange)

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

一个可直接替换的 `binkw32.dll` 代理，拦截应用程序与真实 Bink DLL 之间的 Bink 视频 API 调用。通过 ordinal 加载真实 DLL 并透明地转发所有函数调用。

最初开发用于在 **Command & Conquer: Red Alert 2 Yuri's Revenge**（模组）中集成异步媒体播放器，但适用于任何使用 Bink 视频 SDK 的应用程序。

## 工作原理

1. 应用程序从工作目录加载 `binkw32.dll`（我们的代理）
2. 首次调用 BinkOpen 时，代理解析游戏可执行文件路径并从同一目录加载真实的 Bink DLL（延迟初始化以避免 loader lock 死锁）
3. 所有 Bink API 函数通过 **ordinal** 从真实 DLL 解析
4. 应用程序调用我们导出的存根，通过 `__stdcall` 函数指针直接转发到真实 DLL

```
gamemd.exe → binkw32.dll (proxy) → binkw32_1.0q.dll (real Bink SDK)
```

## ⚙️ 环境要求

- MSVC（Visual Studio 2022 或更新版本）
- CMake 3.28+

## 🏗️ 编译

```bash
# 构建默认组（5 = RA2/RA2YR 默认，7 = 最佳视频质量）
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# 构建全部 19 组（顺序构建 — 并行构建会导致 binkw32.exp 链接器竞争）
cmake -B build -DBINK_GROUPS="all" -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# 构建指定组
cmake -B build -DBINK_GROUPS="5;7;18" -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

每组输出到 `build/GROUP_N/Release/`，包含：
- `binkw32.dll` — 代理 DLL
- `binkw32_X.Yz.dll` — 真实 Bink DLL（从 `Real/` 复制）
- `binkw32.cfg` — 默认配置（从项目根目录复制）

## 🧪 编译测试

```bash
cmake -B build_tests -DBUILD_TESTS=ON -G "Visual Studio 18 2026" -A Win32
cmake --build build_tests --config Release
```

### ▶️ 运行测试

```bash
# 运行所有测试
build_tests\tests\Release\bink32w_tests.exe

# 运行指定测试套件
build_tests\tests\Release\bink32w_tests.exe --gtest_filter="ScalingTest.*"

# 使用游戏目录运行集成测试
set GAME_DIR=C:\path\to\game
build_tests\tests\Release\bink32w_tests.exe
```

### 📈 测试覆盖率

306 个测试分布在 41 个测试套件中，覆盖所有核心模块：

| 模块 | 测试数 | 覆盖率 |
|------|--------|--------|
| config.cpp（CRC32、.mix 解析器、.bik 头、.wav 解码器、配置解析器） | 78 | 100% |
| binkw32_proxy.cpp（TrackVideo、UntrackVideo、FindVideo、缩放、DLL 生命周期、ExtractFileName、BINKIOPROCESSOR、CCFileClass、BinkSetPan、BinkSetWillLoop、BinkWait） | 89 | 100% |
| wav_player.cpp（分配、释放、启动、停止、暂停、恢复、跳转） | 35 | 100% |
| logging.cpp（Log、LogF、TrimRight） | 13 | 100% |
| audio_decoder.cpp（WAV、OGG、负面测试） | 21 | 100% |
| 损坏数据测试（畸形 .mix、.bik、.wav、配置） | 26 | — |
| 集成测试（DLL 导出、序数、真实文件、WAV 解码） | 14 | — |
| 第三方（OGG、WAV、跨格式、.mix） | 15 | — |

## 📦 安装

1. 将构建好的 `GROUP_N/` 文件夹复制到游戏目录
2. 将其中的 `binkw32.dll` 重命名以替换游戏原始文件
3. 启动游戏

如果真实 DLL 缺失，将出现错误消息对话框。

## 🎮 Bink 版本兼容性

### 支持的版本（19 组，67 个版本）

| 组 | 版本 | 状态 | 示例游戏 |
|----|------|------|----------|
| 1 | 1.8c-1.8x (12) | ✅ | Dragon Age Origins, Mass Effect, BioShock, COD MW2/MW3 |
| 2 | 1.5e-1.5v (10) | ✅ | Beyond Good and Evil, XIII, FarCry, Divine Divinity |
| 3 | 1.5x-1.7b (9) | ✅ | Psychonauts, Evil Genius, RACE On, Kane & Lynch 2 |
| 4 | 1.9a-1.9h (5) | ✅ | PAYDAY The Heist, Mass Effect 2, Tropico 3, The Witcher |
| **5** | **1.0n-1.0t (5)** | **✅** | **RA2 / RA2YR 默认** |
| 6 | 1.9i-1.9p (5) | ✅ | Batman Arkham Asylum, Sleeping Dogs, Dishonored, Borderlands |
| **7** | **1.9q-1.9u (3)** | **✅** | **最佳视频质量** — Portal 2, Just Cause 2, Brink, Duke Nukem Forever |
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

### 排除的版本（37 个版本）

| 版本 | 原因 |
|------|------|
| 0.5a-0.9n | 过于古老，内部崩溃（ntdll 访问冲突） |
| 1.0c-1.0f | BinkOpen 返回 NULL（无法打开 RA2YR 视频文件） |
| 1.2h | BinkSetSoundSystem 后崩溃 |
| 1.8r | BinkMake/BinkMix 工具，非视频 API |
| 1.99a-1.99w, 1.9y-1.9z, 2.1c | 预发布版本，BinkOpen 后崩溃 |
| 2.4i, 2.7g | Bink 2.x，不同内部实现 |

### 兼容组详情

| 组 | 版本 | 序数 | 备注 |
|----|------|------|------|
| 1 | 1.8c-1.8x (12) | BinkControlBackgroundIO | 早期 DX9 |
| 2 | 1.5e-1.5v (10) | BinkCopyToBufferRect, BinkDX8SurfaceType | 2000年代中期 |
| 3 | 1.5x-1.7b (9) | BinkSetMemory, YUV blits | 过渡期 |
| 4 | 1.9a-1.9h (5) | BinkDoFrameAsync, BinkShouldSkip | 1.9u 之前 |
| **5** | **1.0n-1.0t (5)** | **83 序数, ExpandBink, RADSetMemory** | **RA2/RA2YR 默认** |
| 6 | 1.9i-1.9p (5) | BinkDoFramePlane, BinkSetMemory | 1.9x 中期 |
| **7** | **1.9q-1.9u (3)** | **73 序数, BinkSetMemory** | **最佳视频质量** |
| 8 | 1.0v-1.0x (3) | RADSetMemory，无 ExpandBink | 1.0x 后期 |
| 9 | 1.8a-1.8b (2) | BinkControlBackgroundIO, BinkShouldSkip | 早期 DX9 |
| 10 | 1.2i-1.5a (2) | BinkDX8SurfaceType, BinkSetMemory | 早期-中期 |
| 11 | 1.1b-1.2a (2) | BinkDX8SurfaceType, RADSetMemory | 早期 1.x |
| 12 | 1.2c-1.2d (2) | BinkSetMixBins | — |
| 13 | 1.1c (1) | BinkDX8SurfaceType, RADTimerRead | — |
| 14 | 1.0k (1) | 无 BinkSetIO, ExpandBink | — |
| 15 | 1.0m (1) | ExpandBink + ExpandBundleSizes | — |
| 16 | 1.0h (1) | YUV_blit 通用, ExpandBink | — |
| 17 | 1.0i (1) | YUV_blit 通用, ExpandBink, RADTimerRead | — |
| 18 | 1.7d (1) | BinkDX9SurfaceType, 86 序数 | — |
| 19 | 1.0j (1) | 无 ExpandBink，仅 ExpandBundleSizes | — |

## 🎵 音频替换

将任何 `.bik` 视频的音轨替换为自定义 `.wav` 或 `.ogg` 文件。代理通过 LMD（Local Mix Database）CRC32 解析自动检测 `.mix` 归档中的 `.bik` 文件。

### 支持格式

- WAV：PCM，8/16 位，任意采样率，单声道/立体声（最多 8 声道）
- OGG：Vorbis，任意采样率，单声道/立体声（通过 stb_vorbis）
- 相对路径（从 DLL 目录）和绝对路径

### WAV 转 OGG

使用 `tools/convert_wav_to_ogg.ps1` 批量将 WAV 文件转换为 OGG Vorbis。脚本递归扫描所有子文件夹。

```powershell
# 转换当前目录下所有 WAV（递归）
.\tools\convert_wav_to_ogg.ps1

# 转换指定文件夹
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav\files"

# 更高质量（0=最差, 10=最佳, 默认=3）
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -Quality 5

# 预览将要转换的文件（干运行）
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -DryRun

# 转换并删除原始 WAV
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -DeleteOriginal
```

输出示例：
```
Found 1022 WAV files, quality=10
  7wolf\a00_f00e.ogg  41098.5KB -> 10584.9KB (26%)
  7wolf\a01_f00e.ogg  17833.5KB -> 4866.6KB (27%)
  ...
Done: 980 converted, 42 failed
```

### 配置

`binkw32.cfg` 在构建期间复制到输出目录。编辑它以配置音频替换：

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
; enabled = false   ; 禁用所有日志（默认：true）
; wait = true       ; 记录 BinkWait 调用（默认：false）

[audio]
; 全局回退（当 exception 中未找到时使用）
s01_f00e.bik = BinkWAV\s01_f00e.wav
```

### 优先级

`[exception]` 段的优先级**高于** `[audio]`。代理首先检查 `.mix` 归档名是否匹配 exception 条目，然后在该 exception 段中查找 `.bik` 文件名。如果未找到，则回退到全局 `[audio]` 段。

保留的段名（`[audio]`、`[exception]`、`[log]`）不能用作 `.mix` exception 段名。

### 工作流程

1. 调用 `BinkOpen` 时，代理解析 `.mix` 归档头和 LMD
2. CRC32 哈希解析为原始 `.bik` 文件名
3. 文件名先与 `[exception]`（按 `.mix` 名称）匹配，再与 `[audio]` 匹配
4. 如果找到映射，音频文件（`.wav` 或 `.ogg`）被解码为 PCM 并通过 WaveOut 播放
5. 替换视频的 Bink 音频自动静音（`BinkSetVolume` → 0）
6. `BinkClose` 时停止播放

### BINKIOPROCESSOR / CCFileClass 支持

当游戏使用 `BINKIOPROCESSOR` (0x02000000) 标志调用 `BinkOpen` 时，第一个参数是 `CCFileClass*` 指针而非文件名或文件句柄。IHCore 等模组使用此方式从 zip 档案中读取 `.bik` 文件。

代理自动从 `CCFileClass` 提取 `.bik` 文件名，使用两种方式：
1. **Vtable**：通过 vtable[1] 调用 `GetFileName()`（YRpp 中的 FileClass 层级）
2. **Fallback**：直接读取 offset 24 处的 `FileName` 字段（RawFileClass）

两种方式均使用 SEH 保护防止无效内存访问。提取文件名后，代理在所有 `[exception]` 节中搜索匹配的 `.bik` 名称，然后回退到 `[audio]`。

如果文件名提取失败（例如非 CCFileClass 上下文），音频替换被禁用但视频正常播放。

## 📁 .mix 归档解析

代理解析 RA2/YR `.mix` 归档格式：

- 头部：4 字节保留 + offset 4 处的 `uint16` 文件计数
- offset `0xA` 处的哈希表（每条目 12 字节：CRC32 + offset + size）
- LMD 文件（CRC32 `0x366E051F`）包含 CRC32 → 文件名映射
- CRC32 按 RA2 约定计算：大写 + 填充到 4 字节对齐

## 📐 视频缩放

当 `BinkCopyToBuffer` 的目标缓冲区小于视频分辨率时，代理使用**保持宽高比的适配缩放**（类似 CSS `object-fit: contain`）自动缩放帧。视频在目标缓冲区中居中，必要时添加黑边。

缩放使用**带预计算查找表的最近邻算法**以实现最大速度。源视频以全分辨率渲染到临时缓冲区，然后使用查找表高效地复制到游戏缓冲区。DDraw 负责最终的屏幕拉伸——单次插值。

## 📝 日志记录

日志文件 `binkw32_proxy.log` 创建在 DLL 目录。

### 日志选项

在 `binkw32.cfg` 中：

```ini
[log]
enabled = false   ; 禁用所有日志（默认：true）
wait = true       ; 记录 BinkWait 调用（默认：false）
```

## 🔄 @N 参数适配器

某些 Bink 版本对同一 API 有不同的函数签名（例如 `BinkSetVolume@8` vs `@12`）。代理包含包装存根，用于在游戏的导入签名与真实 DLL 签名之间进行适配。

## 📊 调用堆栈日志

当使用文件句柄调用 `BinkOpen` 时，代理记录包含模块和 RVA 信息的调用堆栈，帮助识别游戏代码的哪部分发起了视频播放。

## 🔧 工具配置

所需工具（`dumpbin.exe`、`ffmpeg.exe`）在首次使用时从 GitHub **自动下载**：

```powershell
# 下载所有工具（dumpbin + ffmpeg）
powershell -ExecutionPolicy Bypass -File tools\setup.ps1

# 仅下载 dumpbin
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Tools dumpbin

# 仅下载 ffmpeg
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Tools ffmpeg

# 强制重新下载
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Force
```

如果未找到工具，`generate_ordinals.ps1` 和 `convert_wav_to_ogg.ps1` 将自动下载。

## 🔁 重新生成 ordinal 表

在 `Real/` 中添加新的 Bink DLL 后，重新生成 ordinal 表：

```bash
cd Proxy_Bink32w
powershell -ExecutionPolicy Bypass -File tools\generate_ordinals.ps1
```

参见 `tools/ordinals_map.json` 了解版本→组映射。

## 📂 项目结构

```
Proxy_Bink32w/
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # 音频替换配置
├── Real/                    # 原始 Bink DLL（67 个兼容版本）
│   ├── binkw32_1.0q.dll
│   ├── binkw32_1.9u.dll
│   └── ...
├── tests/                   # Google Test 套件（306 个测试，41 个测试套件）
│   ├── test_proxy_core.cpp  # TrackVideo、UntrackVideo、FindVideo
│   ├── test_uncovered.cpp   # LogCallStack、EnsureInitialized、Scaling、sBinkClose、sBinkPause、sBinkGoto、sBinkSetVolume2、sBinkSetSoundOnOff、ExtractFileName
│   ├── test_binkioprocessor.cpp # BINKIOPROCESSOR 标志处理、ExtractNameFromCCFileClass
│   ├── test_corrupt_data.cpp # 损坏 .mix、.bik、.wav、配置的负面测试
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
│   ├── setup.ps1                # 从 GitHub 自动下载 dumpbin 和 ffmpeg
│   ├── generate_ordinals.ps1    # 从 DLL 自动生成 ordinal 表
│   └── ordinals_map.json        # 版本→组映射
└── src/
    ├── binkw32_proxy.h      # 共享类型、全局变量、函数声明
    ├── binkw32_proxy.cpp    # DLL 加载器、视频跟踪、代理导出
    ├── logging.cpp          # 日志子系统
    ├── config.cpp           # 配置解析、.mix 解析器、Bink 头读取
    ├── audio_decoder.cpp    # 统一 WAV + OGG 解码器（stb_vorbis）
    ├── stb_vorbis.c         # OGG Vorbis 解码器（stb_vorbis v1.22，公共领域）
    ├── wav_player.cpp       # WaveOut 音频播放
    ├── ordinals.inc         # 自动生成的 ordinal 表（19 组）
    ├── exports.def          # DLL 导出表（108 个导出）
    └── version_info.rc      # DLL 版本信息
```

## 🔗 相关项目

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Command & Conquer 的 Bink 视频模组
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — MIX 文件解保护工具，支持 LMD 恢复
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — RAD Game Tools 库
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — 用于游戏模组的 Bink 代理
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — Bink 1 和 2 的异步媒体播放器

## 📜 许可证

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons 署名-非商业性-相同方式共享 4.0 国际许可协议

作者：**YoWassup**
