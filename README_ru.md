# Proxy_Bink32w — Bink Video API Proxy DLL

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4./)
![Platform](https://img.shields.io/badge/Platform-Windows%20(x86)-blue)
![C++](https://img.shields.io/badge/C%2B%2B-17-green)
![Tests](https://img.shields.io/badge/Tests-255%20passed-brightgreen)
![Bink](https://img.shields.io/badge/Bink-67%20versions-orange)

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

Прокси-DLL, перехватывающая вызовы Bink Video API между приложением и реальной Bink DLL. Загружает настоящую DLL по ordinal и прозрачно перенаправляет все функции.

Разработано для интеграции асинхронного медиаплеера в **Command & Conquer: Red Alert 2 Yuri's Revenge** (и модов), но работает с любым приложением, использующим Bink video SDK.

## Как работает

1. Приложение загружает `binkw32.dll` (нашу прокси) из рабочей директории
2. При первом вызове BinkOpen прокси определяет путь к исполняемому файлу и загружает настоящую Bink DLL из той же директории (отложенная инициализация для избежания дедллока loader lock)
3. Все Bink API функции резолвятся **по ordinal** из реальной DLL
4. Приложение вызывает наши экспортируемые стабы, которые перенапрямляют на реальную DLL через `__stdcall` указатели

## ⚙️ Требования

- MSVC (Visual Studio 2022 или новее)
- CMake 3.28+

## 🏗️ Сборка

```bash
# Собрать дефолтные группы (5 = RA2/RA2YR, 7 = лучшее качество видео)
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# Собрать все 19 групп (последовательно — параллельная сборка вызывает гонку линкера на binkw32.exp)
cmake -B build -DBINK_GROUPS="all" -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# Собрать конкретные группы
cmake -B build -DBINK_GROUPS="5;7;18" -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

Каждая группа выводится в `build/GROUP_N/Release/` с:
- `binkw32.dll` — прокси
- `binkw32_X.Yz.dll` — настоящая Bink DLL (скопирована из `Real/`)
- `binkw32.cfg` — дефолтный конфиг (скопирован из корня проекта)

## 🧪 Сборка тестов

```bash
cmake -B build_tests -DBUILD_TESTS=ON -G "Visual Studio 18 2026" -A Win32
cmake --build build_tests --config Release
```

### ▶️ Запуск тестов

```bash
# Запустить все тесты
build_tests\tests\Release\bink32w_tests.exe

# Запустить конкретный тестовый сьют
build_tests\tests\Release\bink32w_tests.exe --gtest_filter="ScalingTest.*"

# Запуск с директорией игры для интеграционных тестов
set GAME_DIR=C:\path\to\game
build_tests\tests\Release\binkw32w_tests.exe
```

### 📈 Покрытие тестами

255 тестов в 31 тестовом сьюте, покрывающих все основные модули:

| Модуль | Тестов | Покрытие |
|--------|--------|----------|
| config.cpp (CRC32, парсер .mix, заголовки .bik, декодер .wav, парсер конфига) | 78 | 100% |
| binkw32_proxy.cpp (TrackVideo, UntrackVideo, FindVideo, scaling, жизненный цикл DLL, ExtractFileName) | 47 | 100% |
| wav_player.cpp (alloc, free, start, stop, pause, resume, seek) | 35 | 100% |
| logging.cpp (Log, LogF, TrimRight, ротация файлов) | 1 | 100% |
| audio_decoder.cpp (WAV, OGG, негативные тесты) | 21 | 100% |
| Тесты на повреждённые данные (битые .mix, .bik, .wav, конфиг) | 26 | — |
| Интеграционные (экспорты DLL, ординалы, реальные файлы) | 13 | — |
| Third-party (OGG, WAV, кросс-формат, .mix) | 15 | — |

## 📦 Установка

1. Скопируйте папку `GROUP_N/` в директорию игры
2. Переименуйте `binkw32.dll` внутри для замены оригинальной DLL игры
3. Запустите игру

Если настоящая DLL отсутствует, появится диалог с сообщением об ошибке.

## 🎮 Совместимость с версиями Bink

### Поддерживаемые версии (19 групп, 67 версий)

| Группа | Версии | Статус | Примеры игр |
|--------|--------|--------|-------------|
| 1 | 1.8c-1.8x (12) | ✅ | Dragon Age Origins, Mass Effect, BioShock, COD MW2/MW3 |
| 2 | 1.5e-1.5v (10) | ✅ | Beyond Good and Evil, XIII, FarCry, Divine Divinity |
| 3 | 1.5x-1.7b (9) | ✅ | Psychonauts, Evil Genius, RACE On, Kane & Lynch 2 |
| 4 | 1.9a-1.9h (5) | ✅ | PAYDAY The Heist, Mass Effect 2, Tropico 3, The Witcher |
| **5** | **1.0n-1.0t (5)** | **✅** | **RA2 / RA2YR по умолчанию** |
| 6 | 1.9i-1.9p (5) | ✅ | Batman Arkham Asylum, Sleeping Dogs, Dishonored, Borderlands |
| **7** | **1.9q-1.9u (3)** | **✅** | **Лучшее качество видео** — Portal 2, Just Cause 2, Brink, Duke Nukem Forever |
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

### Исключённые версии (37 версий)

| Версии | Причина |
|--------|---------|
| 0.5a-0.9n | Слишком старые, краш в ntdll |
| 1.0c-1.0f | BinkOpen возвращает NULL (не может открыть видео RA2YR) |
| 1.2h | Краш после BinkSetSoundSystem |
| 1.8r | BinkMake/BinkMix tool, не видео API |
| 1.99a-1.99w, 1.9y-1.9z, 2.1c | Pre-release, краш после BinkOpen |
| 2.4i, 2.7g | Bink 2.x, другая внутренняя реализация |

### Детали совместимых групп

| Группа | Версии | Ординалы | Примечания |
|--------|--------|----------|------------|
| 1 | 1.8c-1.8x (12) | BinkControlBackgroundIO | Ранний DX9 |
| 2 | 1.5e-1.5v (10) | BinkCopyToBufferRect, BinkDX8SurfaceType | Середина 2000-х |
| 3 | 1.5x-1.7b (9) | BinkSetMemory, YUV blits | Переходные |
| 4 | 1.9a-1.9h (5) | BinkDoFrameAsync, BinkShouldSkip | До 1.9u |
| **5** | **1.0n-1.0t (5)** | **83 ординала, ExpandBink, RADSetMemory** | **RA2 / RA2YR по умолчанию** |
| 6 | 1.9i-1.9p (5) | BinkDoFramePlane, BinkSetMemory | Середина 1.9x |
| **7** | **1.9q-1.9u (3)** | **73 ординала, BinkSetMemory** | **Лучшее качество видео** |
| 8 | 1.0v-1.0x (3) | RADSetMemory, без ExpandBink | Поздний 1.0x |
| 9 | 1.8a-1.8b (2) | BinkControlBackgroundIO, BinkShouldSkip | Ранний DX9 |
| 10 | 1.2i-1.5a (2) | BinkDX8SurfaceType, BinkSetMemory | Ранний-средний |
| 11 | 1.1b-1.2a (2) | BinkDX8SurfaceType, RADSetMemory | Ранний 1.x |
| 12 | 1.2c-1.2d (2) | BinkSetMixBins | — |
| 13 | 1.1c (1) | BinkDX8SurfaceType, RADTimerRead | — |
| 14 | 1.0k (1) | Без BinkSetIO, ExpandBink | — |
| 15 | 1.0m (1) | ExpandBink + ExpandBundleSizes | — |
| 16 | 1.0h (1) | YUV_blit generic, ExpandBink | — |
| 17 | 1.0i (1) | YUV_blit generic, ExpandBink, RADTimerRead | — |
| 18 | 1.7d (1) | BinkDX9SurfaceType, 86 ординалов | — |
| 19 | 1.0j (1) | Без ExpandBink, только ExpandBundleSizes | — |

## 🎵 Замена аудио

Замена аудио-дорожки любого `.bik` видео на пользовательский `.wav` или `.ogg` файл. Прокси автоматически определяет `.bik` файлы внутри `.mix` архивов с помощью разрешения CRC32 хешей из LMD (Local Mix Database).

### Поддерживаемые форматы

- WAV: PCM, 8/16 бит, любая частота дискретизации, моно/стерео (макс. 8 каналов)
- OGG: Vorbis, любая частота дискретизации, моно/стерео (через stb_vorbis)
- Относительные пути (от директории DLL) и абсолютные пути

### Конвертация WAV в OGG

Используйте `tools/convert_wav_to_ogg.ps1` для пакетной конвертации WAV файлов в OGG Vorbis. Скрипт рекурсивно сканирует все подпапки.

```powershell
# Конвертировать все WAV в текущей директории (рекурсивно)
.\tools\convert_wav_to_ogg.ps1

# Конвертировать конкретную папку
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav\files"

# Повышенное качество (0=худшее, 10=лучшее, по умолчанию=3)
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -Quality 5

# Предварительный просмотр (режим сухого запуска)
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -DryRun

# Конвертировать и удалить оригинальные WAV
.\tools\convert_wav_to_ogg.ps1 "C:\path\to\wav" -DeleteOriginal
```

Пример вывода:
```
Found 1022 WAV files, quality=10
  7wolf\a00_f00e.ogg  41098.5KB -> 10584.9KB (26%)
  7wolf\a01_f00e.ogg  17833.5KB -> 4866.6KB (27%)
  ...
Done: 980 converted, 42 failed
```

### Конфигурация

Файл `binkw32.cfg` копируется в выходную директорию при сборке. Редактируйте его для настройки замены аудио:

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
; enabled = false   ; отключить все логирование (по умолчанию: true)
; wait = true       ; логировать вызовы BinkWait (по умолчанию: false)

[audio]
; Глобальный fallback (используется если не найдено в exception)
s01_f00e.bik = BinkWAV\s01_f00e.wav
```

### Приоритет

Секция `[exception]` имеет **приоритет над** `[audio]`. При открытии видео прокси сначала проверяет, совпадает ли имя `.mix` архива с записью в `[exception]`, затем ищет имя `.bik` файла в этой секции. Если не найдено — использует глобальную секцию `[audio]`.

Зарезервированные имена секций (`[audio]`, `[exception]`, `[log]`) нельзя использовать как имена секций `.mix` исключений.

### Как работает

1. При вызове `BinkOpen` прокси парсит заголовок `.mix` архива и LMD
2. CRC32 хеш разрешается в оригинальное имя `.bik` файла
3. Имя сопоставляется с `[exception]` (по имени `.mix`) сначала, затем с `[audio]`
4. Если маппинг найден — аудио файл (`.wav` или `.ogg`) декодируется в PCM и воспроизводится через WaveOut
5. Аудио Bink автоматически отключается (`BinkSetVolume` → 0) для заменённого видео
6. Воспроизведение останавливается при `BinkClose`

## 📁 Парсинг .mix архивов

Прокси парсит формат `.mix` архивов RA2/YR:

- Заголовок: 4 байта зарезервировано + `uint16` количество файлов на offset 4
- Хеш-таблица на offset `0xA` (12 байт на запись: CRC32 + offset + size)
- LMD файл (CRC32 `0x366E051F`) содержит маппинг CRC32 → имя файла
- CRC32 вычисляется по соглашению RA2: верхний регистр + паддинг до кратности 4

## 📐 Масштабирование видео

При вызове `BinkCopyToBuffer` с буфером назначения меньшим, чем разрешение видео, прокси автоматически масштабирует кадр с **сохранением пропорций** (как CSS `object-fit: contain`). Видео центрируется в буфере назначения с чёрными полосами по краям при необходимости.

Масштабирование использует **nearest-neighbor с предвычисленными таблицами поиска** для максимальной скорости. Исходное видео рендерится в полном разрешении во временный буфер, затем эффективно копируется в игровой буфер с помощью таблицы, отображающей каждый пиксель назначения на пиксель источника. DDraw выполняет финальное растяжение на разрешение экрана — одна интерполяция.

## 📝 Логирование

Лог-файл `binkw32_proxy.log` создаётся в директории с DLL.

### Опции лога

В `binkw32.cfg`:

```ini
[log]
enabled = false   ; отключить все логирование (по умолчанию: true)
wait = true       ; логировать вызовы BinkWait (по умолчанию: false)
```

## 🔄 Адаптеры @N параметров

Некоторые версии Bink имеют разные сигнатуры функций для одного API (например, `BinkSetVolume@8` vs `@12`). Прокси включает обёртки, которые адаптируют сигнатуру импорта игры под сигнатуру реальной DLL.

## 📊 Логирование стека вызовов

При вызове `BinkOpen` с хэндлом файла прокси логирует стек вызовов с информацией о модуле и RVA, что помогает определить какая часть кода игры инициировала воспроизведение видео.

## 🔧 Настройка инструментов

Необходимые инструменты (`dumpbin.exe`, `ffmpeg.exe`) **автоматически скачиваются** из GitHub при первом использовании:

```powershell
# Скачать все инструменты (dumpbin + ffmpeg)
powershell -ExecutionPolicy Bypass -File tools\setup.ps1

# Скачать только dumpbin
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Tools dumpbin

# Скачать только ffmpeg
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Tools ffmpeg

# Принудительная перезагрузка
powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -Force
```

Если инструменты не найдены, `generate_ordinals.ps1` и `convert_wav_to_ogg.ps1` автоматически скачают их.

## 🔁 Перегенерация ordinal таблиц

Для перегенерации ordinal таблиц после добавления новых Bink DLL в `Real/`:

```bash
cd Proxy_Bink32w
powershell -ExecutionPolicy Bypass -File tools\generate_ordinals.ps1
```

См. `tools/ordinals_map.json` для маппинга версия→группа.

## 📂 Структура проекта

```
Proxy_Bink32w/
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # Конфиг замены аудио
├── Real/                    # Оригинальные Bink DLL (67 совместимых версий)
│   ├── binkw32_1.0q.dll
│   ├── binkw32_1.9u.dll
│   └── ...
├── tests/                   # Google Test suite (255 тестов, 31 сьют)
│   ├── test_proxy_core.cpp  # TrackVideo, UntrackVideo, FindVideo
│   ├── test_uncovered.cpp   # LogCallStack, EnsureInitialized, Scaling, sBinkClose, ExtractFileName
│   ├── test_corrupt_data.cpp # Негативные тесты для битых .mix, .bik, .wav, конфига
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
│   ├── setup.ps1                # Авто-загрузка dumpbin и ffmpeg из GitHub
│   ├── generate_ordinals.ps1    # Авто-генерация ordinal таблиц из DLL
│   └── ordinals_map.json        # Маппинг версия→группа
└── src/
    ├── binkw32_proxy.h      # Общие типы, глобальные переменные, прототипы
    ├── binkw32_proxy.cpp    # DLL загрузчик, видео-трекинг, proxy экспорты
    ├── logging.cpp          # Подсистема логирования
    ├── config.cpp           # Парсинг конфига, парсер .mix, чтение заголовков Bink
    ├── audio_decoder.cpp    # Единый декодер WAV + OGG (stb_vorbis)
    ├── stb_vorbis.c         # OGG Vorbis декодер (stb_vorbis v1.22, public domain)
    ├── wav_player.cpp       # Воспроизведение аудио через WaveOut
    ├── ordinals.inc         # Авто-генерированные ordinal таблицы (19 групп)
    ├── exports.def          # Таблица экспорта DLL (108 экспортов)
    └── version_info.rc      # Информация о версии DLL
```

## 🔗 Связанные проекты

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Bink видео мод для Command & Conquer
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — Распаковщик .mix архивов с восстановлением LMD
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — библиотеки RAD Game Tools
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — Bink прокси для моддинга игр
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — асинхронный медиаплеер для Bink 1 и 2

## 📜 Лицензия

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

Автор: **YoWassup**
