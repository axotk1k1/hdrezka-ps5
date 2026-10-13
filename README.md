# HdRezka for PS5

Homebrew-клиент для просмотра каталога [HDrezka](https://hdrezka.ag) на jailbroken PS5. Архитектура как у [Orbit Store](https://github.com/saawant12/orbit-store-ps5): **ELF-сервис + 10-foot веб-интерфейс** под DualSense.

Это **неофициальный** клиент. Он не связан с HDrezka и Sony. Используйте только материал, на который у вас есть права.

## Какие API используются

На HDrezka нет публичного REST API. Все три библиотеки, которые вы указали, ходят в одни и те же внутренние эндпоинты сайта. Клиент в `www/js/hdrezka.js` повторяет их.

| Задача | Эндпоинт | Откуда взято |
|---|---|---|
| Быстрый поиск | `POST /engine/ajax/search.php` (`q`) | [SuperZombi/HdRezkaApi](https://github.com/SuperZombi/HdRezkaApi) `HdRezkaSearch`, [n0madic/go-hdrezka](https://github.com/n0madic/go-hdrezka) `QuickSearch` |
| Полный поиск | `GET /search/?do=search&subaction=search&q=` | SuperZombi `find_all=True`, kristal374 `HDrezka.search()`, go-hdrezka `Search` |
| Новинки по типу | `POST /engine/ajax/get_newest_slider_content.php` (`id`: 1 фильмы, 2 сериалы, 3 мульт, 82 аниме) | go-hdrezka `GetCoversNewest` |
| Каталог / обложки | `GET /films/`, `/series/`, `/cartoons/`, `/animation/`, `/new/?filter=popular` | go-hdrezka `GetCovers`, kristal374 навигация |
| Карточка тайтла | HTML страницы: `#post_id`, `#translators-list`, `.b-post__*` | SuperZombi `HdRezkaApi`, kristal374 `movie_page_descriptor` |
| Сезоны / серии | `POST /ajax/get_cdn_series/` `action=get_episodes` | SuperZombi `seriesInfo` |
| Стрим фильма | `POST /ajax/get_cdn_series/` `action=get_movie` | SuperZombi `getStream()`, go-hdrezka `GetStream()` |
| Стрим серии | `POST /ajax/get_cdn_series/` `action=get_stream` + `season` + `episode` | SuperZombi, go-hdrezka |
| Расшифровка URL | соли `//_//` + Base64, как в официальном приложении; запасной вариант SuperZombi `clearTrash` | go-hdrezka `decodeURL`, SuperZombi `clearTrash` |
| Вход | `POST /ajax/login/` `login_name` + `login_password` | SuperZombi `login()` |
| Cookie аккаунта | `dle_user_id` + `dle_password` | SuperZombi `make_cookies` |

Поток ответа (`url`) приходит в обфусцированном виде. После `clearTrash` получается строка вида:

```
[360p]https://cdn…/360.mp4 or https://cdn…/360.mp4:hls:manifest.m3u8,[720p]…
```

MP4 отдаётся в `<video>` через локальный `/stream` (Range / перемотка). HLS на WebKit PS5 ненадёжен, поэтому плеер предпочитает MP4.

Anubis (proof-of-work) прокси решает сам и сохраняет clearance-cookie, руками ничего вставлять не нужно. Cloudflare / DDoS-Guard headless не решаются. Для 1080p Ultra нужен премиум-аккаунт зеркала.

## Как это устроено (как Orbit)

Два артефакта:

1. **Native title** (иконка в Games через ShadowMountPlus) — `eboot.bin` + `sce_module/libc.prx` + `sce_sys/`.
2. **Payload ELF** (websrv / :9021) — `rezka-ps5.elf` + HTTP `:34178` + `www/`.

```
PS5 ELF loader :9021
        │
        ▼
 rezka-ps5.elf  ──HTTP :34178──►  WebKit PS5 / телефон
        │                            │
        │  GET /                     │  10-foot UI (DualSense)
        │  POST /proxy               │  поиск, каталог, карточка
        │  GET /stream?url=…         │  <video> + качество / субтитры
        ▼
   зеркало HDrezka + CDN
```

### Иконка в Games (ShadowMountPlus)

Нужны jailbreak, **kstuff**, [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus).

```bash
cd native
# PATH с llvm21 wrappers: см. native/.tools/bin
make app
```

Готовая папка: `native/dist/PPSA99380/`  
Готовая USB-раскладка: `dist/usb/homebrew/PPSA99380/` — копируйте содержимое `dist/usb/` на корень флешки.

ShadowMountPlus (по умолчанию `scan_depth=1`) видит **только**:

```
/data/homebrew/PPSA99380/eboot.bin
/data/homebrew/PPSA99380/sce_sys/param.json
# или
/mnt/usb0/homebrew/PPSA99380/...
```

Не сработает: `…/homebrew/dist/PPSA99380`, `…/homebrew/native/…`, ZIP целиком, папка без `homebrew/`, вложенный лишний уровень.

Нужны **kstuff** + ShadowMountPlus; после копирования подождите ~15–30 с (scan/stability) или перезапустите SM. Не держите рядом второй источник с тем же `PPSA99380` (старый payload-folder / `.ffpkg`).

Фаза 1: HTTP+proxy **внутри** native title (elfldr не нужен). WebKit открывает `http://<LAN-IP>:34178/` (не `127.0.0.1` — DNS на консоли). В пакете есть `www/`. На экране должно быть `HTTP INPROCESS OK`.

### Payload для websrv / ELF loader

```
/data/homebrew/PPSA99380/     # путь websrv, не путать с native-папкой выше
├── eboot.elf                 # скопируйте rezka-ps5.elf
├── homebrew.js
├── param.json
├── sce_sys/icon0.png
├── www/
└── data/
```
## Запуск на ПК (проверка UI и API)

```bash
cd hdrezka-ps5
python3 host/server.py --port 34178
```

Откройте [http://127.0.0.1:34178/](http://127.0.0.1:34178/). Управление: стрелки, Enter, Escape, `[` `]` — вкладки. Настройки → зеркало, если `hdrezka.ag` не открывается.

Опционально тот же сервис на libcurl:

```bash
make -f Makefile.pc
./rezka-pc --root "$PWD" --port 34178
```

## Сборка ELF для PS5

Нужны [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) и libcurl из [pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo).

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make -f Makefile.ps5
```

Загрузка (как websrv / Orbit): сначала [elfldr](https://github.com/ps5-payload-dev/elfldr) на `:9021`, затем:

```bash
nc -q0 "$PS5_HOST" 9021 < rezka-ps5.elf
```

Откройте `http://<ip-ps5>:34178/` в браузере консоли или с телефона в той же сети. Если установлен [websrv](https://github.com/ps5-payload-dev/websrv), положите папку в `/data/homebrew/PPSA99380/` и запускайте с его карусели.

Payload Manager: укажите свой хост ELF в `payloads.json` (поля `url` / `sha256`) и добавьте файл как custom source — формат как у [Orbit payloads.json](https://github.com/saawant12/orbit-store-ps5/blob/main/payloads.json).

## Управление

| Кнопка | Действие |
|---|---|
| D-pad / стрелки | Фокус |
| Cross / Enter | Выбор |
| Circle / Escape | Назад |
| L1 R1 / `[` `]` | Вкладки |
| Space (в плеере) | Пауза |

## Ограничения

- Native title: `cd native && make app` → `dist/PPSA99380/`. Опционально `make ffpkg` (нужен .NET 8). Иконка на главной — только через ShadowMountPlus + kstuff.
- Cloudflare / DDoS-Guard по-прежнему требуют cookie из браузера. Anubis решается автоматически.
- HTML5-плеер PS5 играет MP4; экзотические CDN-кодеки могут не открыться.
- Приложение не обходит DRM легальных сервисов и не поставляет аккаунты.

## Лицензия

MIT. Библиотеки, с которых сняты эндпоинты: HdRezkaApi, hdrezka-api (MIT), go-hdrezka (MIT).
