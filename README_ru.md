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
2. Скопируйте соответствующую настоящую Bink DLL
3. Запустите игру

## Замена аудио

Замена аудио-дорожки любого `.bik` видео на пользовательский `.wav` файл. Прокси автоматически определяет `.bik` файлы внутри `.mix` архивов с помощью разрешения CRC32 хешей из LMD (Local Mix Database).

### Конфигурация

Создайте `binkw32.cfg` в директории с DLL:

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
4. Если маппинг найден — `.wav` файл загружается и воспроизводится через WaveOut
5. Аудио Bink автоматически отключается (`BinkSetVolume` → 0) для заменённого видео
6. Воспроизведение `.wav` останавливается при `BinkClose`

### Поддерживаемые форматы

- WAV файлы: PCM, 8/16/24 бит, любая частота дискретизации, моно/стерео
- Относительные пути (от директории DLL) и абсолютные пути

## Парсинг .mix архивов

Прокси парсит формат `.mix` архивов RA2/YR:

- Заголовок: 4 байта зарезервировано + `uint16` количество файлов на offset 4
- Хеш-таблица на offset `0xA` (12 байт на запись: CRC32 + offset + size)
- LMD файл (CRC32 `0x366E051F`) содержит маппинг CRC32 → имя файла
- CRC32 вычисляется по соглашению RA2: верхний регистр + паддинг до кратности 4

## Масштабирование видео

При вызове `BinkCopyToBuffer` с буфером назначения меньшим, чем разрешение видео, прокси автоматически масштабирует кадр с **сохранением пропорций** (как CSS `object-fit: contain`). Видео центрируется в буфере назначения с чёрными полосами по краям при необходимости.

- Билинейная интерполяция для 4 bpp (32-бит) поверхностей
- Ближайший сосед для 2/3 bpp поверхностей

## Логирование

Лог-файл `binkw32_proxy.log` создаётся в директории с DLL.

### Отключение логирования

1. **Файл** — создайте пустой файл `binkw32.nolog` в директории с DLL
2. **Переменная окружения** — установите `BINK_PROXY_LOG=0`

### Опции лога

В `binkw32.cfg`:

```ini
[log]
wait = true    ; логировать вызовы BinkWait (по умолчанию: false)
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
    ├── binkw32_proxy.cpp    # Основная прокси + аудио + парсер .mix
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
