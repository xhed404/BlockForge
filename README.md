# BlockForge Launcher

![BlockForge Banner](https://chatgpt.com/backend-api/estuary/public_content/enc/eyJpZCI6Im1fNjlmNjQwZTEyMWJjODE5MWE4YzgwZWVhMTVkN2ZlZWI6ZmlsZV8wMDAwMDAwMGUxZjQ3MjBhOWY4NmE2MmI4YTZhZmMxMiIsInRzIjoiMjA1NzUiLCJwIjoicHlpIiwiY2lkIjoiMSIsInNpZyI6ImU0MTA0ZmFiYTM4YTI5OTVhN2VhNWZkODA5MjUyZjU0YjRiNTQ1ZjRiOGU1YmQ3Zjc1NWU1MzMzYjIxNGFjNjIiLCJ2IjoiMCIsImdpem1vX2lkIjpudWxsLCJjcyI6bnVsbCwiY2RuIjpudWxsLCJmbiI6bnVsbCwiY2QiOm51bGwsImNwIjpudWxsLCJtYSI6bnVsbH0=)

BlockForge — лаунчер Minecraft с поддержкой vanilla / Fabric / Forge, оффлайн-профилей и Microsoft login.

## Возможности

- Инстансы: создание, импорт/экспорт
- Моды: включение/выключение (через `.jar.disabled`)
- Аккаунты: offline + Microsoft (device code)

## Скачать (Windows)

Собранные артефакты публикуются в Releases (`Portable` и `Setup`, плюс `SHA256SUMS`).

## Быстрый старт

1) Запустите BlockForge  
2) Создайте инстанс (vanilla/fabric/forge)  
3) Укажите Java (если нужно) и RAM  
4) Нажмите Play

## Online-mode (Microsoft)

Для online-mode используется Microsoft device-code flow. `client_id` уже задан по умолчанию и при необходимости меняется в UI.

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

## QA (Windows)

См. чеклист: [docs/windows_qa_checklist.md](docs/windows_qa_checklist.md).

## Лицензия

MIT, см. [LICENSE](LICENSE).
