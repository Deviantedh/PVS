# PVS GUI skeleton

`gui/` — начальный статический каркас web UI для визуальной работы с симулятором.

Текущий статус:

- без npm-зависимостей;
- без build step;
- отправляет `model.Job` на `POST /api/run`;
- создает live/debug session через `POST /api/session`;
- умеет делать replay-backed `step/run/stop` через session API;
- подписывается на SSE events, если браузер поддерживает `EventSource`;
- отправляет session-local pin control через `POST /api/session/{id}/pins/{name}`;
- показывает текущий `model.Result`, включая CPU/peripheral/pin snapshot;
- фиксирует контракт: GUI общается с Go service, а не с C simulator напрямую.

## Как посмотреть

Открыть файл напрямую в браузере:

```sh
open gui/index.html
```

Или поднять любой статический сервер из корня репозитория:

```sh
python3 -m http.server 8090
```

После этого открыть:

```text
http://localhost:8090/gui/
```

## Целевая интеграция

```text
GUI -> Go HTTP API -> runner -> pvs_sim_cli -> sim engine
```

Первый backend endpoint для подключения UI:

```text
POST /api/run
```

Он принимает firmware/config и возвращает result JSON со статусом, количеством инструкций, UART output и снимком состояния CPU/peripherals.

Локальный запуск backend:

```sh
cd service
go run ./cmd/pvs-http --simulator ../build/sim/pvs_sim_cli --addr 127.0.0.1:8080
```

После этого открыть `gui/index.html`, оставить `Backend URL` равным `http://127.0.0.1:8080/api/run` и нажать `Run simulator`.

Для live/session flow нажать `Create session`, затем `Step` или `Run session`. Клик по пину отправляет минимальный pin control request. На текущем этапе это session-local overlay и он не влияет на C-симулятор, потому что GPIO input path еще не реализован.
