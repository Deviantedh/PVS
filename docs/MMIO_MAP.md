# MMIO Map

Статус: Implemented subset

Документ фиксирует текущую карту памяти и MMIO-регистров симулятора. Если поведение еще не реализовано в `sim`, оно отмечено как planned.

## Memory regions

| Region | Base | Size | Status | Notes |
|---|---:|---:|---|---|
| Flash | `0x08000000` | `64 KiB` default | implemented | Raw firmware is loaded here. Reset vector is read from `base + 0x00` and `base + 0x04`. |
| SRAM | `0x20000000` | `20 KiB` default | implemented | Used for data and MSP stack. |
| Peripherals | `0x40000000` | device-defined | partial | TIM2 and USART1 are routed by the bus for 32-bit accesses. |
| SCS | `0xE000E000` | `0x1000` | planned | NVIC/SCB/SysTick MMIO registers are not routed through the bus yet. |

## IRQ numbers

IRQ numbers are centralized in `sim/include/sim/irq.h`.

| Interrupt | IRQ number | Exception number | Status |
|---|---:|---:|---|
| TIM2 | `28` | `44` | implemented |
| USART1 | `37` | `53` | reserved/planned |

## TIM2

Base address: `0x40000000`

| Offset | Register | Access | Implemented behavior |
|---:|---|---|---|
| `0x00` | `CR1` | RW | `CEN` bit starts/stops counting. |
| `0x0C` | `DIER` | RW | `UIE` enables update interrupt pending. |
| `0x10` | `SR` | RW | `UIF` is set on overflow. Writing `0` clears it. |
| `0x24` | `CNT` | RW | Current counter value. |
| `0x28` | `PSC` | RW | Prescaler value. Internal prescaler counter resets on write. |
| `0x2C` | `ARR` | RW | Auto-reload value. |

Implemented bits:

| Register | Bit mask | Name |
|---|---:|---|
| `CR1` | `0x0001` | `CEN` |
| `DIER` | `0x0001` | `UIE` |
| `SR` | `0x0001` | `UIF` |

## USART1

Base address: `0x40013800`

| Offset | Register | Access | Implemented behavior |
|---:|---|---|---|
| `0x00` | `SR` | RW | `TXE` is set in the minimal model. `RXNE` storage exists, RX flow is planned. |
| `0x04` | `DR` | RW | Write transmits one byte when `UE` and `TE` are set. |
| `0x08` | `BRR` | RW | Stored, timing is not modeled. |
| `0x0C` | `CR1` | RW | Stores `UE`, `TE`, `RE`, and `RXNEIE`. |

Implemented bits:

| Register | Bit mask | Name |
|---|---:|---|
| `SR` | `0x0020` | `RXNE` |
| `SR` | `0x0080` | `TXE` |
| `CR1` | `0x0004` | `RE` |
| `CR1` | `0x0008` | `TE` |
| `CR1` | `0x0020` | `RXNEIE` |
| `CR1` | `0x2000` | `UE` |

## Access policy

- The simulator is little-endian.
- Unaligned halfword and word accesses return a bus fault result.
- Current TIM2/USART1 MMIO routing supports 32-bit bus accesses.
- Unsupported MMIO registers return `BUS_STATUS_UNMAPPED`.

## Planned additions

- Route NVIC registers through SCS MMIO (`0xE000E100` range).
- Add SysTick/SCB stubs in SCS.
- Add USART1 RX input buffer and `RXNEIE` interrupt delivery.
- Decide whether byte/halfword accesses to MMIO registers should be supported or faulted.
