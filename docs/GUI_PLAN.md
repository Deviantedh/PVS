# GUI plan

## Цель

GUI нужен как визуальный слой поверх уже существующего симулятора и Go runner. Он не должен вызывать C-симулятор напрямую: граница ответственности остается такой:

```text
GUI frontend -> Go service/API -> simulator CLI/runner -> C sim engine
```

Такой подход оставляет `sim/` детерминированным engine-слоем, а `service/` — местом для API-контракта, очередей, таймаутов, диагностики и будущей интеграции с KeyDB/OTel.

## Выбранный подход

Для MVP выбран web UI как отдельный frontend-каталог `gui/`.

Причины:

- не требует desktop-фреймворка и отдельной упаковки приложения;
- хорошо ложится на будущий Go HTTP API;
- проще тестировать и демонстрировать в учебном проекте;
- можно начать со статической страницы и постепенно подключить реальные endpoints;
- не смешивает UI-код с C engine и Go runner.

На первом шаге `gui/` — статический frontend без сборки и без npm-зависимостей. Он уже умеет отправлять `model.Job` на `POST /api/run` и показывать текущий `model.Result`. Если UI станет сложнее, его можно заменить на Vite/React или другой легкий стек, сохранив API-контракт.

## Роль слоев

### `sim/`

- исполняет firmware;
- моделирует CPU, память, MMIO, NVIC, TIM2, USART1;
- возвращает stop reason, счетчики, UART output и диагностику.

### `service/`

- принимает jobs;
- валидирует и декодирует firmware;
- запускает simulator CLI;
- нормализует результат в JSON;
- в будущем предоставляет HTTP/WebSocket API для GUI.

### `gui/`

- загружает firmware/job config через Go service;
- показывает состояние симуляции;
- отображает UART output;
- показывает CPU/register snapshot и периферию;
- визуализирует пины/линии Blue Pill на уровне состояния, которое отдаст backend.

## Минимальный API-контракт для GUI

Текущий Go runner уже имеет job/result-модель. Поверх него добавлен минимальный HTTP слой.

MVP endpoints:

- `POST /api/run` — синхронно запустить raw/base64 firmware с конфигом и вернуть result.
- `POST /api/session` — создать live/debug session.
- `GET /api/session/{id}` — получить текущий snapshot session.
- `POST /api/session/{id}/step` — выполнить replay-backed шаг/несколько шагов.
- `POST /api/session/{id}/run` — выполнить replay-backed продолжение до лимита.
- `POST /api/session/{id}/stop` — остановить session state.
- `GET /api/session/{id}/events` — SSE stream snapshot-событий.
- `POST /api/session/{id}/pins/{name}` — session-local pin control overlay.

Следующие endpoints понадобятся после появления асинхронного режима:

- `POST /api/jobs` — создать асинхронный job, если понадобится очередь.
- `GET /api/jobs/{job_id}` — получить статус job.
- `GET /api/jobs/{job_id}/result` — получить финальный результат.

Будущий live/debug режим:

- WebSocket можно добавить позже, если понадобится двусторонний realtime. Текущий MVP использует SSE, потому что он есть в стандартном HTTP-стеке Go и не требует внешних зависимостей.

Пример результата, который удобен GUI:

```json
{
  "job_id": "demo",
  "status": "OK",
  "exit_code": 0,
  "instructions_executed": 1000,
  "uart_output": "T",
  "cpu": {
    "pc": 134217760,
    "msp": 536891392,
    "lr": 4294967289,
    "xpsr": 16777216,
    "primask": 0,
    "instr_count": 1000
  },
  "peripherals": {
    "tim2": { "cr1": 1, "psc": 0, "arr": 1, "cnt": 0, "dier": 1, "sr": 1 },
    "usart1": { "sr": 128, "dr": 84, "brr": 0, "cr1": 8200 },
    "nvic": { "selected": -1, "enabled": [28], "pending": [] }
  },
  "pins": [
    { "name": "PA2", "port": "A", "index": 2, "mode": "unknown", "level": null, "label": "USART1_TX" }
  ]
}
```

## Что GUI должен показывать

- состояние запуска: idle/running/completed/failed;
- итоговый stop reason и число выполненных инструкций;
- UART output как главный видимый результат firmware;
- базовые CPU-регистры: PC, MSP, LR, xPSR, PRIMASK;
- статус IRQ/NVIC: enabled, pending, выбранный IRQ;
- состояния TIM2/USART1 на уровне ключевых регистров;
- board view: Blue Pill и pin snapshot (`name`, `port`, `index`, `mode`, `level`, `label`).

## Ограничения MVP

- GUI пока не управляет симуляцией по шагам.
- Нет live stream состояния.
- Нет настоящей модели GPIO/pin mux.
- Нет загрузки ELF, только raw firmware через backend-контракт.
- Статический frontend показывает только те поля, которые уже возвращает `model.Result`.
- `pins` пока строится статически для ограниченного набора Blue Pill пинов; `mode` обычно `unknown`, `level` обычно `null`.
- Session `step/run` пока replay-backed: backend повторно запускает CLI с новым лимитом инструкций, а не держит C simulator state in-process.
- Pin control пока меняет только session snapshot overlay и не влияет на исполнение firmware.

## Ближайшие шаги

1. Добавить in-process simulator/debug bridge или CLI mode, который реально сохраняет состояние между step.
2. Добавить GPIO/MMIO-модель и связать pin control с реальными входами.
3. Добавить WebSocket только если SSE перестанет хватать.
