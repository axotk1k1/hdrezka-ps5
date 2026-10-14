# HDRezka PS5 (hdrezka_vk) — Project Context

## КРИТИЧЕСКОЕ ПРАВИЛО РАБОТЫ С ФАЙЛАМИ
- Для чтения, поиска и редактирования файлов ВСЕГДА используй встроенные инструменты `Read`, `Grep`, `Glob` и `Edit`.
- ЗАПРЕЩЕНО использовать `python3 - << EOF`, `cat`, `sed`, `head`, `tail` через `Bash` для чтения или правки исходного кода (это вызывает лишние проверки безопасности).
- НЕ ищи структуру файлов через `find` или `wc` — используй карту файлов ниже.

## Правило валидации изменений (ОБЯЗАТЕЛЬНО)
- После любого изменения исходного кода C/C++ (.cpp, .hpp, .c, .h) ОБЯЗАТЕЛЬНО запускай сборку (из `hdrezka_vk/`):
  `ninja -C build/ps5-host ps5-samples-host`
- Не завершай задачу и не рапортуй пользователю, пока сборка не пройдет чисто (без ошибок). Если есть ошибки — исправляй их сразу сам.

## 1. Карта файлов проекта
- `hdrezka_vk/` — основной проект C++ / Vulkan.
- `decompiled/HDrezkaTV-v1.4.0/` — декомпилированный референсный APK (`sources/` — код Java/Compose, `resources/res/` — ресурсы и строки).
- `hdrezka_vk/examples/hdrezka/`:
  - `kit/screen.cpp` — весь UI (Главная, Hero-баннер, Тайтл, Плеер, Поиск, Профиль, Комментарии, Настройки), навигация D-pad и отрисовка.
  - `hdrezka_app.hpp` / `hdrezka_app.cpp` — синглтон `rezka::App`, состояния, фоновые потоки (`BigThread`), микшер, закладки, история.
  - `rezka_api.hpp` / `rezka_api.cpp` — сетевой клиент `rezka::Client` (curl), парсинг HTML/AJAX, `get_streams`.
  - `video_player.hpp` / `video_player.cpp` — FFmpeg плеер (раздельные потоки видео и аудио, `sws_scale`).
  - `texture_loader.hpp` / `texture_loader.cpp` — асинхронная загрузка текстур в Vulkan.
- `hdrezka_vk/ps5/`:
  - `src/platform.h`, `src/platform.c` — PS5 слой (аудио, геймпад, `libSceImeDialog`, `libSceAudioIn`).
  - `src/platform_host.c` — слой для Linux PC-хоста.
  - `assets/` и `assets.json` — шрифты Roboto, атлас `tv/icons.png`, иллюстрации `tv/ill-*.png`.
- `hdrezka_vk/klog/` — скриншоты проверок (`tv-home/`, `tv-title/`, `tv-profile/`).

## 2. Сборка и запуск (выполнять из `hdrezka_vk/`)
- **Сборка для PC (хост):** `ninja -C build/ps5-host ps5-samples-host`
- **Запуск на PC (lavapipe):** `env -u VK_INSTANCE_LAYERS VK_LOADER_DRIVERS_SELECT="*lvp*" ps5/tools/host-reference.sh`
- **Сборка для PS5:** `ps5/tools/build.sh` (выход в `dist/PPSA99380/eboot.bin`).

## 3. Архитектура UI
- Пересчёт размеров из APK: `1 dp = 2 px` для `1920x1080`.
- Тема Material TV Dark (фон `#191C1E`), шрифты Roboto Regular/Medium в слотах `fonts.pixel` и `fonts.hand`.
