# Proxy_Bink32w — Bink Video API Proxy DLL

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/)
![Platform](https://img.shields.io/badge/Platform-Windows%20(x86)-blue)
![C++](https://img.shields.io/badge/C%2B%2B-17-green)
![Tests](https://img.shields.io/badge/Tests-438%20passed-brightgreen)
![Bink](https://img.shields.io/badge/Bink-67%20versions-orange)

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

Прокси-DLL, перехватывающая вызовы Bink Video API между приложением и реальной Bink DLL. Загружает настоящую DLL по ordinal и прозрачно перенаправляет все функции.

Разработано для интеграции асинхронного медиаплеера в **Command & Conquer: Red Alert 2 Yuri's Revenge** (и модов), но работает с любым приложением, использующим Bink video SDK.

## Как работает

1. Приложение загружает `binkw32.dll` (нашу прокси) из рабочей директории
2. При первом вызове BinkOpen прокси определяет путь к исполняемому файлу и загружает настоящую Bink DLL из той же директории (отложенная инициализация для избежания дедллока loader lock)
3. Все Bink API функции резолвятся **по ordinal** из реальной DLL
4. Приложение вызывает наши экспортируемые стабы, которые перенаправляют на реальную DLL через `__stdcall` указатели

```
gamemd.exe → binkw32.dll (прокси) → binkw32_1.0q.dll (настоящая Bink SDK)
```

## ⚙️ Требования

- MSVC (Visual Studio 2022 или новее; примеры сборки используют генератор `Visual Studio 18 2026` — подставьте генератор своей установленной VS)
- CMake 3.28+

Для запуска не нужен Visual C++ Redistributable: прокси и тестовый исполняемый файл линкуют **статическую** рантайм-библиотеку (`/MT`, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`), поэтому `binkw32.dll` зависит только от `KERNEL32.dll`/`WINMM.dll`.

## 🏗️ Сборка

```bash
# Собрать дефолтные группы (5 = RA2/RA2YR, 7 = лучшее качество видео)
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release

# Собрать все 19 групп (у каждой группы свой каталог GROUP_N/, параллельная сборка безопасна)
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
build_tests\tests\Release\bink32w_tests.exe
```

### 📈 Покрытие тестами

438 теста в 50 тестовых сьютах, покрывающих все основные модули:

| Модуль | Тестов | Покрытие |
|--------|--------|----------|
| config.cpp (CRC32, парсер .mix, заголовки .bik, декодер .wav, парсер конфига, base dir) | 103 | 100% |
| binkw32_proxy.cpp (TrackVideo, UntrackVideo, FindVideo, scaling, жизненный цикл DLL, ExtractFileName, BINKIOPROCESSOR, CCFileClass, BinkSetPan, BinkSetWillLoop, BinkWait, ReadU32/ReadU16, BppFromFlags, адаптеры арности SetSoundTrack/YUV) | 152 | 100% |
| wav_player.cpp (alloc, free, start, stop, pause, resume, seek) | 55 | 100% |
| logging.cpp (Log, LogF, TrimRight) | 18 | 100% |
| audio_decoder.cpp (WAV, OGG, негативные тесты) | 24 | 100% |
| mix_crypto.cpp (вывод Blowfish-ключа, шифрованные .mix) | 17 | 100% |
| Тесты на повреждённые данные (битые .mix, .bik, .wav, конфиг) | 35 | — |
| Интеграционные (экспорты DLL, ординалы, реальные файлы, декод WAV, конвейер прокси) | 19 | — |
| Third-party (OGG, WAV, кросс-формат, .mix) | 15 | — |

## 📦 Установка

1. Скопируйте папку `GROUP_N/` в директорию игры
2. Переименуйте `binkw32.dll` внутри для замены оригинальной DLL игры
3. Запустите игру

Если настоящая DLL отсутствует, диалога нет: ошибка пишется в `binkw32_proxy.log`, а все вызовы Bink возвращают NULL/failure.

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
| 1.8r | BinkMake/BinkMix — утилиты, не видео API |
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
| 10 | 1.2i-1.5a (2) | BinkDX8SurfaceType, RADSetMemory | Ранний-средний |
| 11 | 1.1b-1.2a (2) | BinkDX8SurfaceType, RADSetMemory | Ранний 1.x |
| 12 | 1.2c-1.2d (2) | BinkSetMixBins | — |
| 13 | 1.1c (1) | BinkDX8SurfaceType, RADTimerRead | — |
| 14 | 1.0k (1) | Без BinkSetIO, ExpandBink | — |
| 15 | 1.0m (1) | BinkSetIO, ExpandBink + ExpandBundleSizes | — |
| 16 | 1.0h (1) | YUV_blit generic, ExpandBink | — |
| 17 | 1.0i (1) | YUV_blit generic, ExpandBink, RADTimerRead | — |
| 18 | 1.7d (1) | BinkDX9SurfaceType, 86 ординалов | — |
| 19 | 1.0j (1) | ExpandBink + ExpandBundleSizes | — |

## 🎵 Замена аудио

Замена аудио-дорожки любого `.bik` видео на пользовательский `.wav` или `.ogg` файл. Прокси автоматически определяет `.bik` файлы внутри `.mix` архивов с помощью разрешения CRC32 хешей из LMD (Local Mix Database).

### Поддерживаемые форматы

- WAV: PCM, 8/16 бит, 1000–192000 Гц, 1–2 канала (лимит waveOut)
- OGG: Vorbis, 1000–192000 Гц, 1–2 канала (через stb_vorbis)
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


> Фрагмент ниже — **иллюстрация всех доступных возможностей**. Поставляемый `binkw32.cfg` — минимальная готовая к использованию версия: три записи авто-base (`|BinkWAV\...`), карты `[movies01]`/`[movies02]` закомментированы, `[audio]` пуст.
```ini
[log]
; enabled = false   ; отключить все логирование (по умолчанию: true)
; wait = true       ; логировать частые вызовы: BinkWait, RADTimerRead, radmalloc, ... (по умолчанию: false)

[exception]
; только имя mix — явные карты в секциях [mix] ниже
0=movies02.mix
; имя mix + базовый каталог — авто: base\stem.ogg, затем base\stem.wav
1=movies01.mix|BinkWAV\RA2
2=movmd03.mix|BinkWAV\RA2YR

[movies01]
; Явный map важнее авто base dir
a01_f00e.bik = custom\a01_f00e.wav

[movies02]
a01_f00e.bik = BinkWAV\a01_f00e.wav
a02_f00e.bik = BinkWAV\a02_f00e.ogg

[audio]
; Глобальный fallback (используется если не найдено в exception)
s01_f00e.bik = BinkWAV\s01_f00e.wav
```

### Приоритет

Секция `[exception]` имеет **приоритет над** `[audio]`. При открытии видео прокси сначала проверяет, совпадает ли имя `.mix` архива с записью в `[exception]`, затем ищет имя `.bik` файла в этой секции. Если не найдено — использует глобальную секцию `[audio]`.

**Base dir (`mix|base`):** каждая запись exception может содержать свой относительный каталог после `|`. Для `.bik` без явного map прокси сначала резолвит `base\stem.ogg`, затем `base\stem.wav` (проверка существования относительно каталога DLL). Если нет ни того ни другого — fallthrough в `[audio]`. Записи без `|` имеют пустой baseDir (auto skipped). Быстрая смена аудиопака: меняется только путь после `|`.

Зарезервированные имена секций (`[audio]`, `[exception]`, `[log]`) нельзя использовать как имена секций `.mix` исключений.

**Порядок секций:** секция `[exception]` должна идти **до** всех секций `.mix` (`[movies01]` и т.д.) — секции `.mix` сопоставляются со списком исключений в момент парсинга; секция, объявленная до `[exception]` (или не объявленная там вовсе), игнорируется, и в лог пишется `WARNING: section [name] ignored — no matching entry in [exception]`.

**Расположение `[log]`:** секция `[log]` применяется предварительным проходом по всему файлу, поэтому может стоять в любом месте: всё, что логируется при разборе остального конфига, уже учитывает `enabled = false`. Ключи распознаются только `enabled` и `wait` (иначе — `WARNING: unknown [log] key '...'`).

### Как работает

1. При вызове `BinkOpen` прокси парсит заголовок `.mix` архива и LMD
2. CRC32 хеш разрешается в оригинальное имя `.bik` файла
3. Имя сопоставляется с `[exception]` (по имени `.mix`: явный map, затем base dir) сначала, затем с `[audio]`
4. Если маппинг найден — аудио файл (`.wav` или `.ogg`) декодируется в PCM и воспроизводится через WaveOut
5. Аудио Bink автоматически отключается (`BinkSetVolume` → 0) для заменённого видео
6. Воспроизведение останавливается при `BinkClose`

### Поддержка BINKIOPROCESSOR / CCFileClass

Когда игра использует флаг `BINKIOPROCESSOR` (0x02000000) с `BinkOpen`, первый параметр — это указатель `CCFileClass*` вместо имени файла или хэндла. Это используется IHCore и аналогичными модами для чтения `.bik` файлов из zip-архивов.

Прокси автоматически извлекает имя `.bik` файла из `CCFileClass` двумя способами:
1. **Vtable**: вызывает `GetFileName()` через vtable[1] (иерархия FileClass из YRpp)
2. **Fallback**: читает поле `FileName` напрямую по offset 24 (RawFileClass)

Оба подхода защищены SEH от некорректного доступа к памяти. После извлечения имени прокси ищет совпадение по всем секциям `[exception]`, затем по `[audio]`.

Если извлечение имени не удалось (например, не CCFileClass), замена аудио отключается, но видео воспроизводится нормально.

## 📁 Парсинг .mix архивов

Прокси парсит формат `.mix` архивов RA2/YR:

- Заголовок: 4 байта зарезервировано + `uint16` количество файлов на offset 4
- Хеш-таблица на offset `0xA` (12 байт на запись: CRC32 + offset + size)
- LMD файл (CRC32 `0x366E051F`) содержит маппинг CRC32 → имя файла
- CRC32 вычисляется по соглашению RA2: верхний регистр + паддинг до кратности 4
- Шифрованные архивы (флаг заголовка `& 2`): Blowfish-ECB — ключ выводится из 80-байтового поля `key_source` заголовка (offset 4) через встроенный открытый RSA-ключ Westwood, в результате получается 56-байтовый Blowfish-ключ (`src/mix_crypto.cpp`), расшифрованный индекс парсится как обычно

## 📐 Масштабирование видео

При вызове `BinkCopyToBuffer` с буфером назначения меньшим, чем разрешение видео, прокси автоматически масштабирует кадр с **сохранением пропорций** (как CSS `object-fit: contain`). Видео центрируется в буфере назначения; окружающая область остаётся незаполненной и потому автоматически выглядит как чёрные полосы (как правило, чёрные) — явная заливка не выполняется.

Масштабирование использует **nearest-neighbor с предвычисленными таблицами поиска** для максимальной скорости. Исходное видео (например, 1400×1080) рендерится в полном разрешении во временный буфер, затем эффективно копируется в игровой буфер с помощью таблицы, отображающей каждый пиксель назначения на пиксель источника. DDraw выполняет финальное растяжение на разрешение экрана — одна интерполяция.

## 📝 Логирование

Лог-файл `binkw32_proxy.log` создаётся в директории с DLL. Ротация выполняется автоматически при старте и когда файл превышает **10 МБ**: `.log` → `.log.1` → … → `.log.9`, самый старый файл удаляется.

### Опции лога

В `binkw32.cfg`:

```ini
[log]
enabled = false   ; отключить все логирование (по умолчанию: true)
wait = true       ; логировать частые вызовы: BinkWait, RADTimerRead, radmalloc, ... (по умолчанию: false)
```

Принимаемые значения булевых ключей: `true` / `1` / `yes` / `on` и `false` / `0` / `no` / `off` (иное значение даёт `WARNING: [key] value ... not recognised`, остаётся значение по умолчанию). `;` или `#` с пробелом перед ними начинают комментарий в конце строки; в путях вида `C:\a;b` они остаются частью значения. Отсутствие `binkw32.cfg` сообщается один раз (`Config not found: ...`) и файл ищется заново при следующем открытии видео — созданный позже конфиг подхватится.

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
├── .gitignore               # Файлы, игнорируемые git
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # Конфиг замены аудио
├── Real/                    # Оригинальные Bink DLL (104 файла: 67 поддерживаемых + 37 исключённых версий; вне действия лицензии проекта)
│   ├── binkw32_1.0q.dll
│   ├── binkw32_1.9u.dll
│   └── ...
├── tests/                   # Google Test suite (438 теста, 50 сьютов)
│   ├── test_proxy_core.cpp  # FindVideo, UntrackVideo, ScaleBufs, TrackVideoSummary
│   ├── test_uncovered.cpp   # TrackVideo, LogCallStack, EnsureInitialized, Scaling, адаптеры арности SoundTrack/YUV, sBinkClose, sBinkPause, sBinkGoto, sBinkSetVolume2, sBinkSetSoundOnOff, sBinkSetPan, sBinkSetMixBins, sBinkSetWillLoop, sBinkWait, sBinkDoFrame, BinkOpenWithOptions, ExtractFileName
│   ├── test_binkioprocessor.cpp # Обработка флага BINKIOPROCESSOR, ExtractNameFromCCFileClass
│   ├── test_corrupt_data.cpp # Негативные тесты для битых .mix, .bik, .wav, конфига
│   ├── test_config_parser.cpp
│   ├── test_audio_decoder.cpp
│   ├── test_wav_player.cpp
│   ├── test_bink_container.cpp
│   ├── test_mix_crc32.cpp
│   ├── test_mix_blowfish.cpp # Шифрованные .mix (Blowfish), вывод ключа
│   ├── test_logging.cpp
│   ├── test_integration.cpp
│   ├── test_third_party.cpp
│   └── ...
├── third-party/             # Реальные игровые данные для интеграционных тестов (вне лицензии проекта)
├── tools/
│   ├── setup.ps1                # Авто-загрузка dumpbin и ffmpeg из GitHub
│   ├── generate_ordinals.ps1    # Авто-генерация ordinal таблиц из DLL
│   ├── convert_wav_to_ogg.ps1   # Пакетная конвертация WAV → OGG Vorbis
│   ├── test_bink_minimal.cpp    # Минимальный harness: Bink open/frame/close
│   ├── test_bink_player.cpp     # Интерактивный Bink-плеер (harness)
│   └── ordinals_map.json        # Маппинг версия→группа
└── src/
    ├── binkw32_proxy.h      # Общие типы, глобальные переменные, прототипы
    ├── binkw32_proxy.cpp    # DLL загрузчик, видео-трекинг, proxy экспорты
    ├── logging.cpp          # Подсистема логирования
    ├── config.cpp           # Парсинг конфига, парсер .mix, чтение заголовков Bink
    ├── mix_crypto.{h,cpp}   # Расшифровка шифрованных .mix (Blowfish, flags & 2)
    ├── mix_blowfish_s0..s3.inl # S-boxes Blowfish (из ReSource)
    ├── audio_decoder.h      # Структура DecodedAudio, прототип DecodeAudioFile
    ├── audio_decoder.cpp    # Единый декодер WAV + OGG (stb_vorbis)
    ├── stb_vorbis.c         # OGG Vorbis декодер (stb_vorbis v1.22, public domain)
    ├── wav_player.cpp       # Воспроизведение аудио через WaveOut
    ├── ordinals.inc         # Авто-генерированные ordinal таблицы (19 групп)
    ├── exports.def          # Таблица экспорта DLL (111 экспортов)
    └── version_info.rc      # Информация о версии DLL
```

## 🔗 Связанные проекты

**Референсы по игре / MIX (на них построен этот прокси):**

- [Ritanlisa/RA2YR_ReSource](https://github.com/Ritanlisa/RA2YR_ReSource) — декомпилированный код RA2YR; источник истины по MIX/CRC/Bink
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — распаковщик .mix архивов с восстановлением LMD
- [secsome/CCFileSystem](https://github.com/secsome/CCFileSystem) — C# парсер MIX (legacy/extended/Blowfish, CRC32)
- [Phobos-developers/YRpp](https://github.com/Phobos-developers/YRpp) — C++ интерфейсы игровых классов (`MixFileClass`, CRC-движок)
- [Everything-Compatible/YRDict](https://github.com/Everything-Compatible/YRDict) — реализации движка WW, компаньон к YRpp
- [SethGekco/YR-Hook-Encyclopedia](https://github.com/SethGekco/YR-Hook-Encyclopedia) — реестр 3700+ адресов игры и hook-фреймворков

**binkw32-прокси / загрузчики (идеи и кросс-чек):**

- [Daodan DLL](https://wiki.oni2.net/Daodan_DLL) — носитель модов для Oni на хайджаке binkw32 (секции конфига, CLI-переопределения)
- [Erik-JS/masseffect-binkw32](https://github.com/Erik-JS/masseffect-binkw32) — binkw32-прокси с поддержкой ASI-загрузчика
- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Bink видео мод для Command & Conquer (субтитры/оверлей)
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — binkw32-прокси для моддинга игр
- [ThirteenAG/Ultimate-ASI-Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) — стандарт де-facto ASI-прокси-загрузчик (может занять и binkw32 — не ставить вместе с этим прокси)
- [elishacloud/dxwrapper](https://github.com/elishacloud/dxwrapper) — фреймворк-обёртка/stub-DLL с богатым INI-конфигом

## ⚠️ Известные ограничения

- Имена экспорта фиксированы списком из `exports.def` (всего 111). Функции громкости/панорамы/микширования экспортированы **в обеих** арностях — `_BinkSetVolume@8` и `@12`, `_BinkSetPan@8` и `@12`, `_BinkSetMixBins@8` и `@16` — поэтому загружается игра с любой из этих декораций, а внутренний стаб подгоняет арность под ту реальную DLL, с которой собрана группа. Любая другая декорация, которой нет в `exports.def`, по-прежнему не позволит загрузить прокси.
- Импорт по ординалу не поддерживается — ординалы самого прокси закреплены в `exports.def` и намеренно не совпадают с оригинальными DLL (в реальной `binkw32_1.0q.dll` `_BinkOpen@8` имеет ординал 34, у прокси — `@4`); таблицы `ordinals.inc` используются только внутри прокси, чтобы находить функции в реальной DLL. Игры должны импортировать по именам (RA2/RA2YR так и делают).
- Три внутренних экспорта, которые есть только в Bink **1.0h/1.0i**, намеренно не входят в сгенерированные таблицы: `_ExpandPlane@44`, `_YUV_blit@56`, `_YUV_blit_mask@56` (в 1.0j они снова отсутствуют). Это вспомогательные функции рендеринга/декодирования, которые не импортирует ни одна игра, поэтому `tools/generate_ordinals.ps1` не перечисляет их в `$knownFunctions`. Их добавление увеличило бы `exports.def` с 111 до 114 экспортов и потребовало бы пересборки всех 19 групп.
- До 8 одновременных замен WAV/OGG (`MAX_WAV_PLAYERS`; слот освобождается при `BinkClose`, поэтому последовательное воспроизведение не ограничено). Замена стартует на **первом вызове `BinkDoFrame`** (или `BinkDoFrameAsync` — прокси трактует его как вызов кадра), а не в момент `BinkOpen`. Если все слоты заняты в этот момент, замена не запускается (в лог: `Failed to start WAV playback`), и видео остаётся немым — оригинальное аудио Bink всё равно приглушается (mute привязан к запланированному пути, а не к плееру).
- Ёмкость `binkw32.cfg`: до **64** записей в `[exception]` (`MAX_EXCEPTION_MIXES`), до **256** явных карт на секцию `[mix]` (`MAX_MAPS_PER_MIX`), до **256** записей в `[audio]` (`MAX_AUDIO_MAPS`). Лишние строки отбрасываются с записью `WARNING: ... limit reached` в лог. Авто-base (`mix|base`) лимит карт **не расходует** — резолвится на demand и ограничен только числом `.bik` в архиве.
- Прочие фиксированные капы: **8** слотов кэша разобранных `.mix` (фиксированный размер, без вытеснения — когда слоты кончились, новые `.mix` не парсятся и такие видео остаются с оригинальным аудио, в лог пишется `MixArchive cache full`), **32** одновременно отслеживаемых видео (`MAX_TRACKED`), **65535** записей на `.mix` (`MIX_MAX_FILES`, u16 — лимит формата). Пути (`mixName`, `baseDir`, `wavPath`) молча обрезаются до `MAX_PATH` (260), строки конфига — до 1023 символов, имена секций — до 63 символов.

## 📜 Лицензия

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

### Сторонние бинарные файлы (`Real/`)

Bink DLL из каталога [`Real/`](Real/) (и их копии в каталогах сборки `GROUP_*`) — проприетарные бинарные файлы RAD Game Tools / их правообладателей, включённые **без изменений и без передачи каких-либо прав**; лицензия проекта выше **на них не распространяется**. Они хранятся в репозитории исключительно для совместимости, тестирования и справки и не должны удаляться.

Автор: **YoWassup**
