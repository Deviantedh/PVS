# TODO

## Ближайшие задачи

- Подключить минимальную GPIO/MMIO-модель к pin snapshot и pin control
- Добавить lifecycle cleanup/delete endpoint для stateful debug sessions
- Привязать pin control из session API к реальному GPIO input path симулятора
- При желании добавить CLI smoke test в `CTest`
- Синхронизировать `CPU_ISA_COVERAGE.md` с фактической реализацией CPU
- Добавить `--version` в CLI
- Добавить trace/dump-regs-on-exit option в CLI
- Сделать CLI demo fixture для запуска демонстрационной raw firmware без ручной сборки файла

## Средний приоритет

- Улучшить CLI:
  - удобнее печатать `uart_output`
- Добавить `USART1 IRQ` и минимальный `RX` path
- Добавить более явные integration tests, а не только один большой `smoke_test`
- Добавить/задокументировать политику `byte/halfword MMIO access`

## Низкий приоритет / после MVP

- более полная модель исключений (`SVC`, `HardFault`)
- nested interrupts
- дополнительные Thumb-инструкции по мере необходимости
- Go-микросервис: добавить реальную KeyDB-интеграцию
- При желании заменить/дополнить SSE на WebSocket для live debug режима
- `KeyDB`
- `OpenTelemetry`

## Выполнено

- Базовый Go-микросервис:
  - JSON job/result model
  - subprocess runner для симулятора
  - timeout через context
  - CLI `pvs-runner`
  - интерфейс queue для будущего KeyDB
  - минимальный otel/logging слой
- `NVIC MMIO`:
  - `ISER`
  - `ICER`
  - `ISPR`
  - `ICPR`
  - `IPR`
- Инструкции `CPSIE i` и `CPSID i`
- Демонстрационный firmware-сценарий `hello_uart`
- Демонстрационный firmware-сценарий `timer_irq`
- End-to-end demo `TIM2 IRQ -> USART1`
- Стартовый GUI-план и статический каркас `gui/`
- HTTP endpoint `POST /api/run` в `service/`
- Подключение `gui/app.js` к `POST /api/run`
- Result snapshot для CPU/peripherals в CLI, Go runner и HTTP API
- Минимальный pin snapshot и отображение pins в GUI
- Первый live/session API с SSE events и session-local pin overlay
- Stateful process-backed debug bridge `pvs_sim_debug`
