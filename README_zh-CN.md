# Proxy_Bink32w — Bink Video API Proxy DLL

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

一个可直接替换的 `binkw32.dll` 代理，拦截应用程序与真实 Bink DLL 之间的 Bink 视频 API 调用。通过 ordinal 加载真实 DLL 并透明地转发所有函数调用。

最初开发用于在 **Command & Conquer: Red Alert 2 Yuri's Revenge**（Mental Omega 模组）中集成异步媒体播放器，但适用于任何使用 Bink 视频 SDK 的应用程序。

## 视频对比

> ▶ [原始 Bink 1.0q](https://disk.yandex.ru/i/akwI_OxaeQaPvg) | ▶ [自定义 Bink 1.9u](https://disk.yandex.ru/i/izkeGHOKPlggGw)

## 工作原理

1. 应用程序从工作目录加载 `binkw32.dll`（我们的代理）
2. `DllMain` 解析游戏可执行文件路径，从同一目录加载真实的 Bink DLL（`binkw32_1.0q.dll` 或 `binkw32_1.9u.dll`）
3. 所有 Bink API 函数通过 **ordinal** 从真实 DLL 解析
4. 应用程序调用我们导出的存根，通过 `__stdcall` 函数指针直接转发到真实 DLL

```
gamemd.exe → binkw32.dll（代理）→ binkw32_1.0q.dll（真实 Bink SDK）
gamemd.exe → binkw32.dll（代理）→ binkw32_1.9u.dll（真实 Bink SDK）
```

## 环境要求

- MSVC（Visual Studio 2022 或更新版本）
- CMake 3.28+

## 编译

```bash
cmake -B build -G "Visual Studio 18 2022" -A Win32
cmake --build build --config Release
```

两个编译目标：

| 目标 | 输出 | 说明 |
|---|---|---|
| `binkw32_10q` | `build/BINK_10Q/Release/binkw32.dll` | Bink 1.0q 代理（83 个 ordinal 导出） |
| `binkw32_19u` | `build/BINK_19U/Release/binkw32.dll` | Bink 1.9u 代理（73 个 ordinal 导出） |

## 安装

1. 将对应的 `binkw32.dll` 从 `build/BINK_10Q/` 或 `build/BINK_19U/` 复制到游戏目录
2. 复制对应的真实 Bink DLL：
   - 1.0q 编译：将 `binkw32_1.0q.dll` 放入游戏目录
   - 1.9u 编译：将 `binkw32_1.9u.dll` 放入游戏目录
3. 启动游戏

若缺少真实 DLL，将显示错误对话框。

## 日志记录

日志文件 `binkw32_proxy.log` 创建在 DLL 所在目录。

### 禁用日志

两种方式可在不重新编译的情况下禁用日志：

1. **文件** — 在 DLL 目录中创建空的 `binkw32.nolog` 文件
2. **环境变量** — 设置 `BINK_PROXY_LOG=0`

### 自定义路径

环境变量 `BINK_PROXY_LOG` 可指定自定义的日志文件路径。

## 兼容性

| Bink 版本 | Ordinals | 备注 |
|---|---|---|
| 1.9u | 73 个导出 | BinkSetVolume@12, BinkSetPan@12 |
| 1.0q | 83 个导出 | BinkSetVolume@8, BinkSetPan@8, YUV blit 函数 |

## @N 参数适配器

某些 Bink 版本对相同 API 有不同的函数签名。代理包含包装存根：

**Bink 1.9u：**
- `_BinkSetVolume@8`（2 参数，游戏导入）→ `_BinkSetVolume@12`（3 参数，真实 DLL）+ `0`
- `_BinkSetSoundTrack@4`（1 参数）→ `_BinkSetSoundTrack@8`（2 参数）+ `0`

**Bink 1.0q：**
- `_BinkSetPan@12`（3 参数，游戏导入）→ `_BinkSetPan@8`（2 参数，真实 DLL）

## 项目结构

```
Proxy_Bink32w/
├── CMakeLists.txt           # 两个编译目标：binkw32_10q 和 binkw32_19u
├── LICENSE                  # CC BY-NC-SA 4.0
├── src/
│   ├── binkw32_proxy.cpp    # 主代理：DllMain + LoadDll + 转发存根
│   ├── exports.def          # DLL 导出表
│   └── version_info.rc      # DLL 版本信息
└── README.md
```

## 技术细节

- **平台**：Win32 (x86)
- **调用约定**：所有存根使用 `extern "C" __stdcall`
- **导出**：通过 `.def` 文件定义，带有明确的 `@N` 装饰
- **版本选择**：编译时 `#ifdef BINK_10Q` / `BINK_19U` 选择 ordinal 映射和 DLL 名称
- **DllMain 中不使用 CRT**：使用 `CreateFileA`/`WriteFile` 进行日志记录
- **仅 ordinal 解析**：所有 Bink 函数使用 `GetProcAddress(h, (LPCSTR)ordinal)`

## 许可证

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons 署名-非商业性-相同方式共享 4.0 国际许可协议

作者：**YoWassup**
