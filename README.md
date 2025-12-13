# Desktop Remote (Relay / Agent / Admin)

Лёгкая C++‑связка для удалённого управления: релейный сервер, агент на целевой машине и админ‑клиент.

## Структура проекта
- `relay/` — релейный сервер, приём агентов и админов, проксирование команд, Telegram‑уведомления и пересылка скриншотов.
- `agent/` — агент на целевой машине: выполняет команды, делает скриншоты, блокирует/разблокирует ввод, автопереподключение.
- `admin/` — консольный клиент для администратора: выбор агента, выполнение команд, lock/unlock, скриншот.
- `common/` — общий протокол (`protocol.h`).

## Зависимости
- Компилятор C++17, `pthread` (Linux/macOS).
- Windows: `-lws2_32`, рекомендуется `-static-libgcc -static-libstdc++`.
- Скриншоты на Linux: желательно `scrot` или `gnome-screenshot` (fallback: `import` из ImageMagick).
- macOS: штатная `screencapture`.
- Telegram: curl должен быть доступен на сервере.

## Сборка
Все команды выполнять из корня проекта.

### Linux (gcc/clang)
```bash
g++ -std=c++17 -O2 -I. -o relay_server relay/main.cpp relay/relay_server.cpp -pthread
g++ -std=c++17 -O2 -I. -o remote_agent  agent/main.cpp  agent/agent.cpp  -pthread
g++ -std=c++17 -O2 -I. -o admin_client  admin/main.cpp  admin/admin_client.cpp -pthread
```

### macOS (clang)
```bash
clang++ -std=c++17 -O2 -I. -o relay_server relay/main.cpp relay/relay_server.cpp -pthread
clang++ -std=c++17 -O2 -I. -o remote_agent  agent/main.cpp  agent/agent.cpp  -pthread
clang++ -std=c++17 -O2 -o admin_client  admin/main.cpp  admin/admin_client.cpp -pthread
```

### Windows (MinGW, статические бинарники без DLL)
- Релиз без консоли (агент, чистая система):
```powershell
g++ -std=c++17 -O2 -I. -mwindows -static -static-libgcc -static-libstdc++ ^
  -o remote_agent.exe agent/main.cpp agent/agent.cpp ^
  -lws2_32 -luser32 -lkernel32 -lwinpthread
```
- Отладка с консолью (агент):
```powershell
g++ -std=c++17 -O2 -I. -static -static-libgcc -static-libstdc++ ^
  -o remote_agent_debug.exe agent/main.cpp agent/agent.cpp ^
  -lws2_32 -luser32 -lkernel32 -lwinpthread
```
- Сервер/клиент под MinGW аналогично: заменить цели и исходники (`relay_server.exe`, `admin_client.exe`), флаги те же (`-static -static-libgcc -static-libstdc++ -lws2_32 -lwinpthread`), `-mwindows` использовать только если нужно скрыть консоль.

## Запуск
Порт по умолчанию `9999`. Если занят, сервер и агент пытаются убить процесс, слушающий порт.

### 1) Запуск релея
```bash
./relay_server
# опционально в фоне (Unix): ./relay_server &
```
Сервер отправляет Telegram‑уведомления о подключении/отключении агентов и пересылает скриншоты.

### 2) Запуск агента
- Параметры не требуются: хост релея захардкожен (`213.108.4.126`), порт `9999`, имя устройства берётся из системы.
- Нужны права администратора/root (Windows UAC, sudo на Unix).
- Пример:
```bash
sudo ./remote_agent         # Linux/macOS
./remote_agent.exe          # Windows (UAC запросит права)
```
Агент работает в фоне (на Windows скрытое окно при сборке с `-mwindows`; на Unix — демон).

### 3) Запуск админ‑клиента
```bash
./admin_client            # Linux/macOS
./admin_client.exe        # Windows
```
Команды в клиенте:
- `list` — список агентов
- `select <id>` — выбрать агента
- `lock` / `unlock` — блокировка/разблокировка клавиатуры и мыши на агенте
- `screenshot` — снять скриншот, получить в Telegram и на клиенте
- `<shell>` — выполнить произвольную команду на агенте
- `exit` — выход

## Особенности и поведение
- Автопереподключение агента: при обрыве ждёт 3 секунды и переподключается.
- Таймауты: сокеты ~120 с (для скриншотов), команды завершаются корректно с выводом stderr.
- Telegram: используются `TELEGRAM_BOT_TOKEN` и `TELEGRAM_CHAT_ID`, зашиты в `relay/relay_server.h`.
- Скриншоты: JPEG на Windows, PNG на *nix. На сервере пересылаются в Telegram и клиенту.
- Блокировка ввода (Windows): `BlockInput`; требуется запуск от администратора.

## Быстрый чеклист запуска
1. Собрать бинарники (см. раздел “Сборка”).
2. Запустить `relay_server` (убедиться, что порт 9999 свободен или будет освобождён).
3. Запустить `remote_agent` с правами администратора/root.
4. Запустить `admin_client`, выполнить `list`, затем `select <id>`.
5. Проверить команды: `lock`, `unlock`, `screenshot`, и произвольный shell‑запрос.

## Тревожные сигналы и диагностика
- Если команды/скриншоты не доходят — смотрите логи релея: ошибки send/recv помечают агента оффлайн, агент переподключится.
- Если lock/unlock не действует на Windows — проверьте, что агент запущен с правами администратора.
- Скриншот пустой на Linux — установите `scrot` или `gnome-screenshot`, иначе потребуется ImageMagick (`import`).


