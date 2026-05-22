# Project Tasks

Статус: Active

Этот список сформирован по результатам анализа `docs` и текущей реализации `sim`. При выполнении задач обновляйте чекбоксы здесь же.

## P0 - CPU L1 correctness

- [x] Реализовать `PUSH` / `POP` Thumb16 для `R0-R7`, `LR`, `PC`.
- [x] Реализовать минимальный `BL` Thumb-2 и 32-bit fetch path для вызовов.
- [x] Реализовать базовые logical Thumb16: `AND`, `ORR`, `EOR`.
- [x] Реализовать базовые shifts Thumb16: `LSL`, `LSR`, `ASR`.
- [x] Реализовать `SVC` как контролируемый exception/stop path.
- [ ] Синхронизировать статусы `CPU_ISA_COVERAGE.md` с фактической реализацией.

## P1 - MMIO and peripherals

- [x] Route NVIC registers through SCS MMIO (`0xE000E100` range).
- [x] Add minimal GPIOA MMIO and connect frontend pin control to simulator input pins.
- [ ] Add USART1 RX input buffer and `RXNE`/`RXNEIE` interrupt flow.
- [ ] Add minimal SysTick/SCB stubs in SCS.
- [ ] Decide and document byte/halfword MMIO access policy.

## P1 - Runner and diagnostics

- [x] Expand CLI contract: `--firmware`, `--max-instr`, `--uart-in`, `--uart-out`, `--json-result`.
- [ ] Add `--version`.
- [ ] Add trace/dump-regs-on-exit option.
- [ ] Add structured stop diagnostics for unsupported instructions and faults.

## P2 - Service

- [x] Scaffold Go microservice modules: queue, runner, model, otel.
- [ ] Implement KeyDB job/result contract.
- [x] Add subprocess timeout handling around simulator CLI.
