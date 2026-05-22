# PVS

Симулятор `STM32F103C8T6 (Blue Pill)` для курса по ПВС.

## Что уже реализовано

- базовый симулятор на C с `CMake`
- модель памяти `Flash/SRAM`
- `bus` с `MMIO` и fault model
- загрузка raw firmware во flash и reset sequence из vector table
- CPU MVP для `Thumb16`
- базовые флаги `N/Z/C/V`
- `NVIC`
- `TIM2`
- `USART1`
- минимальная доставка внешних IRQ и возврат из обработчика
- накопление `uart_output`
- smoke/unit-like тесты

## Что поддерживает CPU сейчас

- `NOP`
- `MOVS`, `MOV`
- `ADD`, `SUB`, `CMP`
- `B`, `B<cond>`, `BX LR`
- `LDR literal`
- `LDR/STR`
- `LDRB/STRB`
- `LDRH/STRH`

## Что поддерживает периферия сейчас

- `TIM2`: `CR1`, `PSC`, `ARR`, `CNT`, `DIER`, `SR`, update IRQ
- `USART1`: `SR`, `DR`, `BRR`, `CR1`, TX output buffer

## Go service

В `service/` добавлен базовый микросервис-раннер:

- принимает job JSON (`job_id`, `firmware` base64, `config`)
- декодирует firmware во временный файл
- запускает симулятор как subprocess
- передаёт CLI-контракт `--firmware`, `--max-instr`, `--timeout-ms`, `--uart-in`, `--json-result`
- возвращает result JSON
- предоставляет минимальный HTTP endpoint `POST /api/run` для GUI/local API
- передает в result snapshot CPU/peripherals для GUI
- предоставляет первый live/session API: create/get/step/run/stop/events/pin overlay

Текущий C CLI поддерживает этот контракт для запуска raw firmware и выдачи `uart_output`/`json-result`. `--timeout-ms` и `--uart-in` пока принимаются как часть контракта; фактический RX/timeout enforcement остаются отдельными задачами.

Пример запуска:

```sh
go run ./service/cmd/pvs-runner --simulator ./build-ninja/sim/pvs_sim_cli --job job.json
go run ./service/cmd/pvs-http --simulator ./build/sim/pvs_sim_cli --debugger ./build/sim/pvs_sim_debug --addr 127.0.0.1:8080
```

## Как запускать

### Сборка симулятора

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

### Запуск C CLI

CLI принимает raw firmware и может печатать UART output и JSON result.

```sh
./build/sim/pvs_sim_cli --firmware firmware.bin
./build/sim/pvs_sim_cli --firmware firmware.bin --max-instr 1000
./build/sim/pvs_sim_cli --firmware firmware.bin --json-result result.json --uart-out uart.txt
```

Поддерживаемые основные аргументы:

- `--firmware <path>`
- `--max-instr <n>`
- `--timeout-ms <n>`
- `--uart-in <string>`
- `--uart-out <path>`
- `--json-result <path>`

### Запуск Go runner

```sh
cd service
go test ./...
go run ./cmd/pvs-runner --simulator ../build/sim/pvs_sim_cli --job job.json
```

Минимальный формат `job.json`:

```json
{
  "job_id": "demo-job",
  "firmware": "BASE64_RAW_FIRMWARE",
  "config": {
    "max_instructions": 100000,
    "timeout_ms": 5000,
    "uart_input": ""
  }
}
```

### Запуск Go HTTP API

```sh
cd service
go run ./cmd/pvs-http --simulator ../build/sim/pvs_sim_cli --debugger ../build/sim/pvs_sim_debug --addr 127.0.0.1:8080
```

MVP endpoint:

```text
POST /api/run
```

Тело запроса использует тот же формат `job.json`, что и CLI runner. Ответ — `model.Result` JSON со статусом, кодом ошибки, `uart_output`, числом выполненных инструкций, CPU snapshot и peripheral snapshot.

## Статус

Текущее состояние проекта — рабочий MVP симулятора:

- firmware может настраивать `TIM2` и `USART1` через `MMIO`
- `TIM2` может генерировать pending IRQ через `NVIC`
- CPU может входить в обработчик прерывания и возвращаться обратно
- запись в `USART1->DR` приводит к накоплению `uart_output`
- result JSON содержит CPU/peripheral snapshot и минимальный pin snapshot

## Что еще не завершено

- полноценная GPIO/MMIO-модель и реальные уровни пинов вместо `mode: "unknown"` / `level: null`
- stateful debug bridge сейчас process-backed; in-process C bridge можно рассмотреть позже, если понадобится меньше overhead
- `USART1 RX` и IRQ от USART
- GPIO/pin model для визуализации платы
- KeyDB/OpenTelemetry интеграция из исходного большого плана

## План по интерфейсу

Отдельно планируется графический интерфейс поверх Go-сервиса. Архитектурный план зафиксирован в `docs/GUI_PLAN.md`, стартовый статический каркас лежит в `gui/`.

Выбранный подход: web UI как отдельный frontend поверх Go backend/API. Идея:

- запускать симуляцию не только через CLI, но и через GUI
- визуально работать с состоянием симулируемой платы
- показывать и переключать пины
- отображать UART и результаты выполнения

То есть Go-сервис рассматривается как backend-слой, а GUI как frontend для удобной визуальной работы с симулируемой платой.

`gui/` не требует сборки и отправляет `model.Job` на backend endpoint `POST /api/run`. Посмотреть и запустить MVP можно так:

```sh
open gui/index.html
```
