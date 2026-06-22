# Proxy_Bink32w — Bink Video API Proxy DLL

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

一个可直接替换的 `binkw32.dll` 代理，拦截应用程序与真实 Bink DLL 之间的 Bink 视频 API 调用。通过 ordinal 加载真实 DLL 并透明地转发所有函数调用。

最初开发用于在 **Command & Conquer: Red Alert 2 Yuri's Revenge**（Mental Omega 模组）中集成异步媒体播放器，但适用于任何使用 Bink 视频 SDK 的应用程序。

## 工作原理

1. 应用程序从工作目录加载 `binkw32.dll`（我们的代理）
2. `DllMain` 解析游戏可执行文件路径，从同一目录加载真实的 Bink DLL
3. 所有 Bink API 函数通过 **ordinal** 从真实 DLL 解析
4. 应用程序调用我们导出的存根，通过 `__stdcall` 函数指针直接转发到真实 DLL

## 环境要求

- MSVC（Visual Studio 2022 或更新版本）
- CMake 3.28+

## 编译

```bash
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

## 音频替换

将任何 `.bik` 视频的音轨替换为自定义 `.wav` 或 `.ogg` 文件。代理通过 LMD（Local Mix Database）CRC32 解析自动检测 `.mix` 归档中的 `.bik` 文件。

### 支持格式

- WAV：PCM，8/16/24 位，任意采样率，单声道/立体声
- OGG：Vorbis，任意采样率，单声道/立体声（通过 stb_vorbis）
- 相对路径（从 DLL 目录）和绝对路径

### 配置

在 DLL 目录创建 `binkw32.cfg`：

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

[video]
; 视频缩放模式（默认：bilinear）
; 选项：nearest, bilinear, area, sharpen-area, sharpen-bilinear,
;        scanline, sharpen, color-dither, crt-scanline
scale_mode=bilinear

[audio]
; 全局回退（当 exception 中未找到时使用）
s01_f00e.bik = BinkWAV\s01_f00e.wav
```

### 优先级

`[exception]` 段的优先级**高于** `[audio]`。代理首先检查 `.mix` 归档名是否匹配 exception 条目，然后在该 exception 段中查找 `.bik` 文件名。如果未找到，则回退到全局 `[audio]` 段。

保留的段名（`[audio]`、`[exception]`、`[log]`、`[video]`）不能用作 `.mix` exception 段名。

### 工作流程

1. 调用 `BinkOpen` 时，代理解析 `.mix` 归档头和 LMD
2. CRC32 哈希解析为原始 `.bik` 文件名
3. 文件名先与 `[exception]`（按 `.mix` 名称）匹配，再与 `[audio]` 匹配
4. 如果找到映射，音频文件（`.wav` 或 `.ogg`）被解码为 PCM 并通过 WaveOut 播放
5. 替换视频的 Bink 音频自动静音（`BinkSetVolume` → 0）
6. `BinkClose` 时停止播放

## .mix 归档解析

代理解析 RA2/YR `.mix` 归档格式：

- 头部：4 字节保留 + offset 4 处的 `uint16` 文件计数
- offset `0xA` 处的哈希表（每条目 12 字节：CRC32 + offset + size）
- LMD 文件（CRC32 `0x366E051F`）包含 CRC32 → 文件名映射
- CRC32 按 RA2 约定计算：大写 + 填充到 4 字节对齐

## 视频缩放

当 `BinkCopyToBuffer` 的目标缓冲区小于视频分辨率时，代理使用**保持宽高比的适配缩放**（类似 CSS `object-fit: contain`）自动缩放帧。视频在目标缓冲区中居中，必要时添加黑边。

9 种缩放算法：

| 模式 | 说明 |
|---|---|
| `nearest` | 最近邻插值 |
| `bilinear` | 双线性插值（默认） |
| `area` | 区域平均（盒式滤波） |
| `sharpen-area` | 区域 + 边缘感知锐化 |
| `sharpen-bilinear` | 预锐化 + 双线性 |
| `scanline` | 双线性 + 交替行变暗 |
| `sharpen` | 双线性 + 非锐化蒙版 |
| `color-dither` | Bayer 4x4 抖动 |
| `crt-scanline` | 双线性 + 柔和 CRT 扫描线效果 |

## 日志记录

日志文件 `binkw32_proxy.log` 创建在 DLL 目录。

### 日志选项

在 `binkw32.cfg` 中：

```ini
[log]
enabled = false   ; 禁用所有日志（默认：true）
wait = true       ; 记录 BinkWait 调用（默认：false）
```

## 项目结构

```
Proxy_Bink32w/
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # 音频替换配置
└── src/
    ├── binkw32_proxy.h      # 共享类型、全局变量、函数声明
    ├── binkw32_proxy.cpp    # DLL 加载器、视频跟踪、代理导出
    ├── logging.cpp          # 日志子系统
    ├── config.cpp           # 配置解析、.mix 解析器、Bink 头读取
    ├── audio_decoder.cpp    # 统一 WAV + OGG 解码器（stb_vorbis）
    ├── stb_vorbis.c         # OGG Vorbis 解码器（stb_vorbis v1.22，公共领域）
    ├── wav_player.cpp       # WaveOut 音频播放
    ├── video_scaler.h       # 视频缩放接口
    ├── video_scaler.cpp     # 9 种缩放算法
    ├── exports.def          # DLL 导出表（107 个导出）
    └── version_info.rc      # DLL 版本信息
```

## 相关项目

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Command & Conquer 的 Bink 视频模组
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — MIX 文件解保护工具，支持 LMD 恢复
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — RAD Game Tools 库
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — 用于游戏模组的 Bink 代理
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — Bink 1 和 2 的异步媒体播放器

## 许可证

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons 署名-非商业性-相同方式共享 4.0 国际许可协议

作者：**YoWassup**
