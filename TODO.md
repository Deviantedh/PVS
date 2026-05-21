# TODO

## Ближайшие задачи

- Добавить `NVIC MMIO`:
  - `ISER`
  - `ICER`
  - `ISPR`
  - `ICPR`
  - `IPR`
- Добавить инструкции `CPSIE i` и `CPSID i` для управления `PRIMASK` из firmware
- Сделать демонстрационный firmware-сценарий уровня `hello_uart`
- Сделать демонстрационный firmware-сценарий уровня `timer_irq`
- При желании добавить CLI smoke test в `CTest`

## Средний приоритет

- Улучшить CLI:
  - удобнее печатать `uart_output`
  - возможно добавить trace/debug flags
- Добавить `USART1 IRQ` и минимальный `RX` path
- Добавить более явные integration tests, а не только один большой `smoke_test`

## Низкий приоритет / после MVP

- `NVIC` как полноценный `SCS/MMIO` блок
- более полная модель исключений (`SVC`, `HardFault`)
- nested interrupts
- дополнительные Thumb-инструкции по мере необходимости
- Go-микросервис
- `KeyDB`
- `OpenTelemetry`
