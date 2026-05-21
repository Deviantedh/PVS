---
# Архитектура проекта

Документ описывает целевую архитектуру системы и ключевые интерфейсы модулей.  
Фокус: **C-симулятор STM32F103C8T6** и его интеграция с Go-микросервисом.

---

## 1. Общая схема системы

```text
+-------------------+        +---------------------+        +-------------------+
|   KeyDB Queue     |  <-->  |   Go Microservice   |  --->  |   C Simulator     |
|  (jobs/results)   |        |  (оркестрация)      |        |  (CPU + MMIO)     |
+-------------------+        +---------------------+        +-------------------+
                                      |
                                      v
                            +---------------------+
                            | OpenTelemetry       |
                            | (traces/logs)       |
                            +---------------------+
```
---

## 2. Архитектура симулятора (C)

### 2.1 Цели архитектуры симулятора

1. **Корректность исполнения** ARMv7-M (Thumb/Thumb-2) для MVP-набора.
2. **Чёткие границы модулей**: CPU ↔ память ↔ MMIO ↔ устройства.
3. **Расширяемость**: добавление новых устройств без переписывания ядра.
4. **Диагностика**: управляемая остановка, причины завершения, трассировка.
5. **Детерминизм**: одинаковый результат при одинаковом входе.

---

### 2.2 Концептуальная модель

Симулятор представляет MCU как:

- **CPU** (регистры, декодер, исполнитель, исключения)
- **Memory map** (Flash/RAM)
- **MMIO bus** (маршрутизация обращений в устройства)
- **Devices** (TIM2, USART1, NVIC, GPIO stub, минимальный SCB/SysTick stub)
- **Scheduler / time model** (как “двигается” время и когда тикают устройства)

---

### 2.3 Структура репозитория симулятора

Рекомендуемая структура (ориентир, допускается эквивалентная):

```text
/sim
  /include
    sim/sim.h
    sim/cpu.h
    sim/bus.h
    sim/memory.h
    sim/device.h
    sim/nvic.h
  /src
    sim.c
    cli.c
    memory.c
    bus.c
    cpu/
      cpu.c
      decode.c
      exec.c
      exceptions.c
    devices/
      nvic.c
      scb.c
      systick.c          (stub или минимальная)
      tim2.c
      usart1.c
      gpio_stub.c
    util/
      log.c
      trace.c
  /tests
  CMakeLists.txt
```

### 2.4 Главные сущности и данные

#### 2.4.1 `sim_t` — состояние всего симулятора

Содержит:
- `cpu_state_t cpu`
- `memory_t mem`
- `bus_t bus`
- устройства (NVIC, TIM2, USART1, …)
- параметры запуска (лимиты, политика времени)
- накопители вывода (UART output) и диагностику

Примерная модель:

- `sim_init(...)`
- `sim_load_firmware_bin(...)`
- `sim_reset(...)`
- `sim_step(...)`
- `sim_run(...)`
- `sim_destroy(...)`

#### 2.4.2 `cpu_state_t` — состояние CPU

Минимум для MVP:
- `uint32_t r[13]` (R0–R12)
- `uint32_t msp`
- `uint32_t psp` (опционально, но поле держим для будущего)
- `uint32_t lr`
- `uint32_t pc`
- `uint32_t xpsr` (APSR+IPSR+EPSR)
- `uint32_t primask`
- `uint32_t basepri` (опционально)

Дополнительно (полезно для отладки):
- `uint64_t instr_count`
- `uint32_t last_pc`
- `uint32_t fault_status` / `stop_reason`

---

### 2.5 Модель времени и “тиков”

#### 2.5.1 Базовая политика (MVP)

- 1 выполненная инструкция CPU = **1 “tick” симулятора**.
- После `execute()` симулятор вызывает `devices_tick(1)`.

Это упрощение, достаточное для TIM2/UART/IRQ в MVP.

#### 2.5.2 Расширение (после MVP)

- Разделить “instruction ticks” и “peripheral clock”.
- Ввести коэффициенты (например, `cpu_ticks_per_step`, `apb1_div`, `timer_mul`).
- Перейти на event-driven scheduler (события по ближайшему overflow/tx-ready).

---

### 2.6 Memory map и шина (Bus/MMIO)

#### 2.6.1 Memory map (MVP)

- Flash: `0x0800_0000 .. 0x0800_0000 + FLASH_SIZE`
- SRAM:  `0x2000_0000 .. 0x2000_0000 + SRAM_SIZE`
- Periph/MMIO: `0x4000_0000 ..`
- SCS: `0xE000_E000 .. 0xE000_EFFF` (NVIC/SCB/SysTick)

Размеры `FLASH_SIZE` и `SRAM_SIZE` задаются конфигом (по умолчанию 64KB/20KB).

#### 2.6.2 API шины

Единый интерфейс чтения/записи:

- `uint32_t bus_read32(sim_t*, uint32_t addr, bus_result_t*);`
- `uint16_t bus_read16(...)`
- `uint8_t  bus_read8(...)`
- `void bus_write32(sim_t*, uint32_t addr, uint32_t value, bus_result_t*);` …

`bus_result_t` должен содержать:
- `ok / fault`
- тип fault (bus fault / unaligned / permission)
- диагностику (адрес, операция, размер)

#### 2.6.3 Маршрутизация

Правило:
1) Если адрес попадает в Flash/RAM — работа с `memory_t`.
2) Если адрес в SCS — маршрутизация в SCB/NVIC/SysTick.
3) Если адрес в диапазоне устройства — вызывается `device_read/write(offset)`.

Важно:
- Устройства получают **offset** (addr - base), а не абсолютный addr.
- Неподдержанный регистр: возвращать “reset value” (если известен) или 0, но логировать (debug mode).

---

### 2.7 Интерфейс устройства

#### 2.7.1 `device_vtbl_t`

Каждое устройство должно реализовать:

- `uint32_t (*read32)(void* dev, uint32_t off);`
- `void     (*write32)(void* dev, uint32_t off, uint32_t val);`
- `void     (*tick)(void* dev, uint32_t ticks);`

#### 2.7.2 Регистрация устройств в bus

`bus_map_device(bus_t*, uint32_t base, uint32_t size, device_vtbl_t vtbl, void* dev);`

### 2.8 NVIC и механизм прерываний

#### 2.8.1 Источник истины

- NVIC как устройство в SCS (`0xE000_E100 ..`) — модель регистров и логики.
- Устройства (TIM2/USART1) не “выполняют” прерывание напрямую, а **ставят pending** в NVIC.

#### 2.8.2 API для устройств

- `nvic_set_pending(nvic_t*, int irq);`
- `nvic_clear_pending(nvic_t*, int irq);`
- `bool nvic_is_enabled(nvic_t*, int irq);`

#### 2.8.3 Доставка прерывания (interrupt delivery)

В конце каждого шага симуляции:
1) `irq = nvic_select_next(...)` (учесть enabled+pending+priority+маски)
2) если `irq != -1`:
   - выполнить exception entry (stacking)
   - установить IPSR = exception number
   - PC = vector[irq]
   - pending → active

Правила маски (MVP):
- если `PRIMASK=1` → внешние IRQ не доставлять
- приоритеты: достаточно сравнения старших 4 бит

---

### 2.9 Исключения (Exception entry/return)

#### 2.9.1 Exception entry (MVP)

При входе в исключение:
- стековать R0–R3, R12, LR, PC, xPSR на текущий SP (MSP)
- LR заменить на EXC_RETURN (минимально поддержать возврат в Thread mode, MSP)

#### 2.9.2 Exception return (MVP)

При выполнении возврата:
- распаковать фрейм
- восстановить PC/xPSR
- вернуть IPSR = 0 (Thread mode)

Если полноценный EXC_RETURN не успеваем — фиксируем ограничение в документации, но механизм должен работать хотя бы для простых IRQ handlers.

---

### 2.10 TIM2 (обязательный таймер)

Модель таймера должна быть **stateful** и привязана к ticks.

Алгоритм (MVP):
- накопитель `presc_accum += ticks`
- пока `presc_accum >= (PSC+1)`:
  - `presc_accum -= (PSC+1)`
  - `CNT++`
  - если `CNT > ARR`:
    - `CNT = 0`
    - `SR.UIF = 1`
    - если `DIER.UIE = 1` → `nvic_set_pending(TIM2_IRQn)`

---

### 2.11 USART1 (обязательный UART)

#### 2.11.1 TX

- запись в `DR` при `UE && TE`:
  - добавить байт в `uart_output` (буфер симулятора)
  - выставить `SR.TXE` (можно сразу 1; либо 0→1 через ticks)
  - если `CR1.TXEIE && SR.TXE` → pending IRQ (опционально для MVP)

#### 2.11.2 RX

- входной буфер `uart_input` задаётся конфигом job
- при наличии входных данных:
  - загрузить байт в `DR`
  - `SR.RXNE=1`
  - если `CR1.RXNEIE` → `nvic_set_pending(USART1_IRQn)`
- чтение `DR` сбрасывает `RXNE`

### 2.12 Точки остановки и диагностика

Симулятор должен завершаться управляемо с `stop_reason`:

- `STOP_OK` (например, при достижении max_instructions)
- `STOP_TIMEOUT`
- `STOP_UNSUPPORTED_INSTR`
- `STOP_FAULT` (HardFault/BusFault/UsageFault — допускается агрегировать)
- `STOP_ASSERT`

Минимальная диагностика:
- `pc`, `lr`, `xpsr`, `instr_count`
- последние N значений PC (ring buffer) в debug режиме
- сообщение ошибки

### 2.13 CLI контракт симулятора (для Go runner)

Симулятор должен иметь CLI, чтобы Go мог запускать как subprocess.

Предлагаемый контракт (MVP):

- `--firmware <path>` (обяз.)
- `--max-instr <n>` (обяз. или default)
- `--timeout-ms <n>` (default)
- `--uart-in <string>` (optional)
- `--uart-out <path>` (optional; если нет — stdout)
- `--json-result <path>` (optional; структурированный результат)

Exit codes:
- `0` — OK
- `10` — TIMEOUT
- `11` — UNSUPPORTED_INSTR
- `12` — FAULT/CRASH
- `13` — BAD_INPUT/CONFIG

## 3. Архитектура микросервиса (Go) — кратко

### 3.1 Модули

- `queue/` — получение job и публикация result
- `runner/` — запуск симулятора, таймауты, сбор stdout/stderr
- `otel/` — трассировка и логирование
- `model/` — структуры job/result + (де)сериализация

### 3.2 Поток обработки job

1) dequeue job
2) decode firmware (base64) → temp file
3) run simulator subprocess
4) parse json-result (если есть) + собрать stdout
5) publish result
6) emit trace spans

---


## 4. Дополнительные архитектурные уточнения (рекомендуется зафиксировать)

### 4.1 Endianness и выравнивание

- Архитектура ARMv7-M работает в **Little Endian** режиме (фиксируется в симуляторе).
- Невыровненный доступ:
  - MVP: допускается либо поддержка unaligned LDR/STR (как в Cortex-M3),
  - либо генерация BusFault при невыровненном доступе (если упрощаем).
- Политика должна быть зафиксирована и задокументирована в `CPU_ISA_COVERAGE.md`.

---

### 4.2 Модель Fault'ов

Минимально поддержать:

- HardFault (агрегирует BusFault/UsageFault в MVP)
- Fault при:
  - обращении к несуществующему MMIO-адресу
  - неподдержанной инструкции
  - ошибке декодирования

Рекомендуется:

- иметь `fault_context_t` с:
  - fault_address
  - access_type (read/write/execute)
  - instruction_word
  - pc_at_fault

---

### 4.3 Политика детерминизма

Симулятор должен быть полностью детерминированным:

- Нет случайных чисел
- Нет зависимости от системного времени
- Все входы (UART, лимиты, конфиг) задаются явно

Это важно для:
- повторяемых тестов
- CI
- сравнения трасс выполнения

---

### 4.4 Режимы запуска

Рекомендуется поддержать:

1. **Normal mode** — выполнение до лимита/завершения.
2. **Step mode** — выполнение по одной инструкции (для отладки).
3. **Trace mode** — логирование каждой инструкции (опционально, под флагом).

CLI-флаг (пример):
- `--trace`
- `--step`
- `--dump-regs-on-exit`

---

### 4.5 Формат внутренней трассы (опционально)

Если включён trace-mode, формат строки может быть:

```
PC=0x08000124  INST=0xF04F0301  R0=... R1=... APSR=...
```

Рекомендуется:
- сделать trace отключаемым compile-time флагом
- либо через runtime-флаг

---

### 4.6 Конфигурационная модель

Возможные параметры конфигурации симулятора:

- FLASH_SIZE
- SRAM_SIZE
- MAX_INSTRUCTIONS (default)
- TIMER_CLOCK_DIV
- UART_IN_BUFFER_SIZE

Рекомендуется вынести их в:

- `sim_config_t`
- или в отдельный header `sim_config.h`

---

### 4.7 Стратегия расширения ISA

Чтобы избежать переписывания CPU при расширении набора инструкций:

- разделить:
  - decode layer
  - semantic execution layer
- для каждой инструкции:
  - выделить отдельную функцию `exec_<mnemonic>()`
- завести таблицу соответствия opcode → handler

Это позволит:
- добавлять инструкции постепенно
- писать unit tests на каждую инструкцию

---

### 4.8 Интеграция с тестами прошивок

Рекомендуется предусмотреть:

- возможность запуска встроенных тестовых прошивок без Go-микросервиса
- директорию `/sim/tests/firmware/`
- make-target типа:
  - `make test-firmware`

---

### 4.9 Версионирование симулятора

Добавить:

- `--version`
- вывод версии при старте
- фиксировать версию в JSON-result

Это упрощает:
- отладку
- сравнение разных сборок
- CI

---

## 5. Следующие шаги (рекомендуемые)

1) [x] Зафиксировать IRQ numbers (TIM2_IRQn, USART1_IRQn) в одном заголовке (`sim/include/sim/irq.h`).
2) [x] Создать документ `docs/MMIO_MAP.md` с base addresses и offsets регистров.
3) [x] Создать `docs/CPU_ISA_COVERAGE.md` (таблица покрытия инструкций).
4) [x] Сразу написать 3 тестовые прошивки + интеграционный тест-раннер.
   - [x] `hello_uart`
   - [x] `timer_irq`
   - [x] `loop_counter`
   - [x] integration runner (`sim/tests/firmware_integration_test.c`)

---





