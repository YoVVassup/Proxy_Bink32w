# Proxy_Bink32w — Bink Video API Proxy DLL

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

Прокси-DLL, перехватывающая вызовы Bink Video API между приложением и реальной Bink DLL. Загружает настоящую DLL по ordinal и прозрачно перенаправляет все функции.

Разработано для интеграции асинхронного медиаплеера в **Command & Conquer: Red Alert 2 Yuri's Revenge** (мод Mental Omega), но работает с любым приложением, использующим Bink video SDK.

## Как работает

1. Приложение загружает `binkw32.dll` (нашу прокси) из рабочей директории
2. `DllMain` определяет путь к исполняемому файлу и загружает настоящую Bink DLL (`binkw32_1.0q.dll` или `binkw32_1.9u.dll`) из той же директории
3. Все Bink API функции резолвятся **по ordinal** из реальной DLL
4. Приложение вызывает наши экспортируемые стабы, которые перенапрямляют на реальную DLL через `__stdcall` указатели

## Требования

- MSVC (Visual Studio 2022 или новее)
- CMake 3.28+

## Сборка

```bash
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

## Установка

1. Скопируйте нужный `binkw32.dll` из `build/BINK_10Q/` или `build/BINK_19U/` в директорию игры
2. Скопируйте соответствующую настоящую Bink DLL:
   - Для сборки 1.0q: положите `binkw32_1.0q.dll` в директорию игры
   - Для сборки 1.9u: положите `binkw32_1.9u.dll` в директорию игры
3. Запустите игру

Если настоящая DLL отсутствует, появится диалог с сообщением об ошибке.

## Замена аудио

Замена аудио-дорожки любого `.bik` видео на пользовательский `.wav` или `.ogg` файл. Прокси автоматически определяет `.bik` файлы внутри `.mix` архивов с помощью разрешения CRC32 хешей из LMD (Local Mix Database).

### Поддерживаемые форматы

- WAV: PCM, 8/16/24 бит, любая частота дискретизации, моно/стерео
- OGG: Vorbis, любая частота дискретизации, моно/стерео (через stb_vorbis)
- Относительные пути (от директории DLL) и абсолютные пути

### Конфигурация

Создайте `binkw32.cfg` в директории с DLL:

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
; Режим масштабирования видео (по умолчанию: bilinear)
; Доступные: nearest, bilinear, area, sharpen-area, sharpen-bilinear,
;            scanline, sharpen, color-dither, crt-scanline
scale_mode=bilinear

[audio]
; Глобальный fallback (используется если не найдено в exception)
s01_f00e.bik = BinkWAV\s01_f00e.wav
```

### Приоритет

Секция `[exception]` имеет **приоритет над** `[audio]`. При открытии видео прокси сначала проверяет, совпадает ли имя `.mix` архива с записью в `[exception]`, затем ищет имя `.bik` файла в этой секции. Если не найдено — использует глобальную секцию `[audio]`.

Зарезервированные имена секций (`[audio]`, `[exception]`, `[log]`, `[video]`) нельзя использовать как имена секций `.mix` исключений.

### Как работает

1. При вызове `BinkOpen` прокси парсит заголовок `.mix` архива и LMD
2. CRC32 хеш разрешается в оригинальное имя `.bik` файла
3. Имя сопоставляется с `[exception]` (по имени `.mix`) сначала, затем с `[audio]`
4. Если маппинг найден — аудио файл (`.wav` или `.ogg`) декодируется в PCM и воспроизводится через WaveOut
5. Аудио Bink автоматически отключается (`BinkSetVolume` → 0) для заменённого видео
6. Воспроизведение останавливается при `BinkClose`

## Парсинг .mix архивов

Прокси парсит формат `.mix` архивов RA2/YR:

- Заголовок: 4 байта зарезервировано + `uint16` количество файлов на offset 4
- Хеш-таблица на offset `0xA` (12 байт на запись: CRC32 + offset + size)
- LMD файл (CRC32 `0x366E051F`) содержит маппинг CRC32 → имя файла
- CRC32 вычисляется по соглашению RA2: верхний регистр + паддинг до кратности 4

## Масштабирование видео

При вызове `BinkCopyToBuffer` с буфером назначения меньшим, чем разрешение видео, прокси автоматически масштабирует кадр с **сохранением пропорций** (как CSS `object-fit: contain`). Видео центрируется в буфере назначения с чёрными полосами по краям при необходимости.

9 алгоритмов масштабирования:

| Режим | Описание |
|---|---|
| `nearest` | Ближайший сосед |
| `bilinear` | Билинейная интерполяция (по умолчанию) |
| `area` | Среднее по области (box filter) |
| `sharpen-area` | Область + повышение резкости по краям |
| `sharpen-bilinear` | Предварительное повышение резкости + билинейная |
| `scanline` | Билинейная + затемнение чередующихся строк |
| `sharpen` | Билинейная + нерезкая маска |
| `color-dither` | Дизеринг Bayer 4x4 |
| `crt-scanline` | Билинейная + мягкий эффект CRT-строк |

## Логирование

Лог-файл `binkw32_proxy.log` создаётся в директории с DLL.

### Опции лога

В `binkw32.cfg`:

```ini
[log]
enabled = false   ; отключить все логирование (по умолчанию: true)
wait = true       ; логировать вызовы BinkWait (по умолчанию: false)
```

## Логирование стека вызовов

При вызове `BinkOpen` с хэндлом файла прокси логирует стек вызовов с информацией о модуле и RVA, что помогает определить какая часть кода игры инициировала воспроизведение видео.

## Структура проекта

```
Proxy_Bink32w/
├── CMakeLists.txt
├── LICENSE                  # CC BY-NC-SA 4.0
├── README.md                # English
├── README_ru.md             # Русский
├── README_zh-CN.md          # 简体中文
├── README_zh-TW.md          # 繁體中文
├── binkw32.cfg              # Конфиг замены аудио
└── src/
    ├── binkw32_proxy.h      # Общие типы, глобальные переменные, прототипы
    ├── binkw32_proxy.cpp    # DLL загрузчик, видео-трекинг, proxy экспорты
    ├── logging.cpp          # Подсистема логирования
    ├── config.cpp           # Парсинг конфига, парсер .mix, чтение заголовков Bink
    ├── audio_decoder.cpp    # Единый декодер WAV + OGG (stb_vorbis)
    ├── stb_vorbis.c         # OGG Vorbis декодер (stb_vorbis v1.22, public domain)
    ├── wav_player.cpp       # Воспроизведение аудио через WaveOut
    ├── video_scaler.h       # Интерфейс видео-скейлера
    ├── video_scaler.cpp     # 9 алгоритмов масштабирования
    ├── exports.def          # Таблица экспорта DLL (107 экспортов)
    └── version_info.rc      # Информация о версии DLL
```

## Связанные проекты

- [dev-zetta/BikMod](https://github.com/dev-zetta/BikMod) — Bink видео мод для Command & Conquer
- [Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer](https://github.com/Aldrin-John-Olaer-Manalansan/RA2YR-reMIXer) — Распаковщик .mix архивов с восстановлением LMD
- [vogonsorg/radgametools](https://github.com/vogonsorg/radgametools) — библиотеки RAD Game Tools
- [americusmaximus/Yoink](https://github.com/americusmaximus/Yoink) — Bink прокси для моддинга игр
- [dimhotepus/Bink-1-and-2-async-media-player](https://github.com/dimhotepus/Bink-1-and-2-async-media-player) — асинхронный медиаплеер для Bink 1 и 2

## Лицензия

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

Автор: **YoWassup**
