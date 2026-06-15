# Proxy_Bink32w — Bink Video API Proxy DLL

[English](README.md) | [Русский](README_ru.md) | [繁體中文](README_zh-TW.md) | [简体中文](README_zh-CN.md)

Прокси-DLL, перехватывающая вызовы Bink Video API между приложением и реальной Bink DLL. Загружает настоящую DLL по ordinal и прозрачно перенаправляет все функции.

Разработано для интеграции асинхронного медиаплеера в **Command & Conquer: Red Alert 2 Yuri's Revenge** (мод Mental Omega), но работает с любым приложением, использующим Bink video SDK.

## Видео сравнения

> ▶ [Оригинальный Bink 1.0q](https://disk.yandex.ru/i/akwI_OxaeQaPvg) | ▶ [Кастомный Bink 1.9u](https://disk.yandex.ru/i/izkeGHOKPlggGw)

## Как работает

1. Приложение загружает `binkw32.dll` (нашу прокси) из рабочей директории
2. `DllMain` определяет путь к исполняемому файлу и загружает настоящую Bink DLL (`binkw32_1.0q.dll` или `binkw32_1.9u.dll`) из той же директории
3. Все Bink API функции резолвятся **по ordinal** из реальной DLL
4. Приложение вызывает наши экспортируемые стабы, которые перенапрямляют на реальную DLL через `__stdcall` указатели

```
gamemd.exe → binkw32.dll (прокси) → binkw32_1.0q.dll (настоящий Bink SDK)
gamemd.exe → binkw32.dll (прокси) → binkw32_1.9u.dll (настоящий Bink SDK)
```

## Требования

- MSVC (Visual Studio 2022 или новее)
- CMake 3.28+

## Сборка

```bash
cmake -B build -G "Visual Studio 18 2022" -A Win32
cmake --build build --config Release
```

Два таргета:

| Таргет | Выход | Описание |
|---|---|---|
| `binkw32_10q` | `build/BINK_10Q/Release/binkw32.dll` | Прокси для Bink 1.0q (83 ordinal экспорта) |
| `binkw32_19u` | `build/BINK_19U/Release/binkw32.dll` | Прокси для Bink 1.9u (73 ordinal экспорта) |

## Установка

1. Скопируйте нужный `binkw32.dll` из `build/BINK_10Q/` или `build/BINK_19U/` в директорию игры
2. Скопируйте соответствующую настоящую Bink DLL:
   - Для сборки 1.0q: поместите `binkw32_1.0q.dll` в директорию игры
   - Для сборки 1.9u: поместите `binkw32_1.9u.dll` в директорию игры
3. Запустите игру

При отсутствии реальной DLL будет показан диалог с ошибкой.

## Логирование

Лог-файл `binkw32_proxy.log` создаётся в директории с DLL.

### Отключение логирования

Два способа отключить логирование без пересборки:

1. **Файл** — создайте пустой файл `binkw32.nolog` в директории с DLL
2. **Переменная окружения** — установите `BINK_PROXY_LOG=0`

### Кастомный путь

Переменная окружения `BINK_PROXY_LOG` задаёт произвольный путь к лог-файлу.

## Совместимость

| Bink Version | Ordinals | Примечания |
|---|---|---|
| 1.9u | 73 экспорта | BinkSetVolume@12, BinkSetPan@12 |
| 1.0q | 83 экспорта | BinkSetVolume@8, BinkSetPan@8, YUV blit функции |

## Адаптеры @N параметров

Некоторые версии Bink имеют разные сигнатуры для одинаковых API. Прокси включает обёртки:

**Bink 1.9u:**
- `_BinkSetVolume@8` (2 параметра, импорт игры) → `_BinkSetVolume@12` (3 параметра, реальная DLL) + `0`
- `_BinkSetSoundTrack@4` (1 параметр) → `_BinkSetSoundTrack@8` (2 параметра) + `0`

**Bink 1.0q:**
- `_BinkSetPan@12` (3 параметра, импорт игры) → `_BinkSetPan@8` (2 параметра, реальная DLL)

## Структура проекта

```
Proxy_Bink32w/
├── CMakeLists.txt           # Два таргета: binkw32_10q и binkw32_19u
├── LICENSE                  # CC BY-NC-SA 4.0
├── src/
│   ├── binkw32_proxy.cpp    # Основная прокси: DllMain + LoadDll + стабы
│   ├── exports.def          # Таблица экспорта DLL
│   └── version_info.rc      # Информация о версии DLL
└── README.md
```

## Технические детали

- **Платформа**: Win32 (x86)
- **Calling convention**: Все стабы используют `extern "C" __stdcall`
- **Экспорты**: Определены через `.def` файл с явными `@N` декорациями
- **Выбор версии**: Compile-time `#ifdef BINK_10Q` / `BINK_19U` выбирает маппинг ordinal-ов и имя DLL
- **Без CRT в DllMain**: Используются `CreateFileA`/`WriteFile` для логирования
- **Ordinal-only резолюция**: `GetProcAddress(h, (LPCSTR)ordinal)` для всех Bink функций

## Лицензия

[CC BY-NC-SA 4.0](LICENSE) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

Автор: **YoWassup**
