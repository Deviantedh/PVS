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

Текущий C CLI поддерживает этот контракт для запуска raw firmware и выдачи `uart_output`/`json-result`. `--timeout-ms` и `--uart-in` пока принимаются как часть контракта; фактический RX/timeout enforcement остаются отдельными задачами.

Пример запуска:

```sh
go run ./service/cmd/pvs-runner --simulator ./build-ninja/sim/pvs_sim_cli --job job.json
```

## Статус

Текущее состояние проекта — рабочий MVP симулятора:

- firmware может настраивать `TIM2` и `USART1` через `MMIO`
- `TIM2` может генерировать pending IRQ через `NVIC`
- CPU может входить в обработчик прерывания и возвращаться обратно
- запись в `USART1->DR` приводит к накоплению `uart_output`

## Что еще не завершено

- `NVIC MMIO`
- инструкции `CPSIE/CPSID`
- более полноценный `CLI`
- демонстрационные firmware-сценарии уровня `hello_uart` / `timer_irq`
- Go-микросервис из исходного большого плана
