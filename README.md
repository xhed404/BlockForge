# BlockForge Launcher

BlockForge - лаунчер Minecraft с поддержкой vanilla / Fabric / Forge, оффлайн-профилей и Microsoft login.

## Что такое лаунчер?

Лаунчер - это программа-оболочка, которая подготавливает среду перед запуском приложения. Она управляет файлами, версиями, настройками, зависимостями и параметрами запуска, а потом уже стартует саму основную программу. В контексте игр лаунчер обычно отвечает за выбор версии, профиля, модов, путей к ресурсам и аргументов запуска.

## Возможности этого лаунчера

- Инстансы: создание, импорт/экспорт
- Моды: включение/выключение (через `.jar.disabled`)
- Аккаунты: offline + Microsoft (device code)

## Скачать (Windows)

Собранные артефакты публикуются в Releases (`Portable` и `Setup`, плюс `SHA256SUMS`).

## Быстрый старт, всего 4 шага!

1) Запустите BlockForge  
2) Создайте инстанс (vanilla/fabric/forge)  
3) Укажите Java (если нужно) и RAM  
4) Нажмите Play

## Online-mode (Microsoft)

Для online-mode используется Microsoft device-code flow. `client_id` уже задан по умолчанию и при необходимости меняется в UI. Если же Вы делаете форк лаунчера, то Вы должны поменять `client_id`, заданный по умолчанию.

## Сборка

### Core + CLI (без Qt)

```bash
cmake -S . -B build -DBLOCKFORGE_BUILD_UI=OFF -DBLOCKFORGE_BUILD_MC=OFF
cmake --build build
./build/blockforge-cli --help
```

### UI + Minecraft integration (Qt 6)

Нужен Qt 6 (Widgets + Network).

```bash
cmake -S . -B build -DBLOCKFORGE_BUILD_UI=ON -DBLOCKFORGE_BUILD_MC=ON
cmake --build build
```

## Лицензия

MIT, см. [LICENSE](LICENSE).

## Состояние проекта

На данный момент в репозитории не рабочий код, если же Вы хотите использовать этот лаунчер, то воспользуйтесь [Releases](https://github.com/xhed404/BlockForge/releases/).
