# PROJECT_CONTEXT.md

## 1. Общая идея проекта
Проект — симулятор микроконтроллера STM32F103C8T6 (ARM Cortex-M3) на языке C с опциональным сервисом на Go.

Назначение проекта:
- Учебная реализация архитектуры ARMv7-M (Thumb) с акцентом на понимание ISA и модели прерываний.
- Формирование навыков системного проектирования (модули, границы ответственности, контракты).
- Практика тестирования (unit/integration) и трассировки.

Не-цели (важно):
- Не требуется 100% бинарная совместимость со всеми особенностями STM32.
- Допускается поэтапная реализация (MVP → расширение).

---

## 2. Аппаратная база
STM32F103C8T6:
- ARM Cortex-M3 (ARMv7-M)
- Flash / RAM
- GPIO, UART, Таймеры, NVIC

Ключевые блоки, подлежащие моделированию:
- CPU: ARM Cortex-M3 (Thumb-2 ISA)
- Память: Flash (код), SRAM (данные), memory-mapped регистры периферии
- NVIC: приоритеты, pending/active состояния
- SysTick: системный таймер
- UART: буферы RX/TX, регистры состояния
- GPIO (опционально): порты ввода/вывода

---

## 3. Архитектура процессора

### Регистры
- R0–R12 — общего назначения
- R13 (SP) — указатель стека
- R14 (LR) — адрес возврата
- R15 (PC) — счетчик команд

### Статус (APSR)
- N (Negative), Z (Zero), C (Carry), V (Overflow)

### Типы данных
- 8/16/32 бита (byte/halfword/word)
- 64 бита через пары регистров

### Особенности ARMv7-M
- Только Thumb (16/32-bit инструкции)
- Memory-mapped архитектура
- Детерминированное исполнение, низкая латентность прерываний

### Модель исполнения
- Fetch → Decode → Execute
- Обновление флагов
- Доступ к памяти через шину (bus)

---

## 4. Цель симулятора

- Исполнение инструкций
- Периферия:
  - Таймер
  - UART
  - NVIC

---

## 5. Архитектура проекта

```
simulator/
├── cpu/           # ядро CPU (fetch/decode/execute)
├── memory/        # память и карта адресов
├── peripherals/   # NVIC, UART, Timer, GPIO
├── bus/           # маршрутизация обращений CPU → memory/peripherals
├── loader/        # загрузка бинарника (raw/ELF)
├── debugger/      # трассировка, дампы регистров
└── tests/         # unit/integration тесты
```

Контракты модулей:
- cpu ↔ bus: чтение/запись по адресу (read32/write32 и аналоги)
- bus ↔ memory/peripherals: маршрутизация по диапазонам адресов
- peripherals ↔ nvic: генерация прерываний

---

## 6. CPU структура

```
cpu/
├── core.c        # главный цикл/step
├── registers.c   # регистры и флаги
├── decode.c      # декодирование инструкций
├── execute.c     # диспетчер выполнения
├── alu.c         # арифметика/логика
├── branch.c      # переходы/ветвления
├── loadstore.c   # LDR/STR
```

Ключевые функции:
- cpu_step(): выполняет одну инструкцию
- fetch(): чтение инструкции по PC
- decode(): определение opcode/operands
- execute(): выполнение и обновление PC/флагов

---

## 7. Execution loop

```c
while (running) {
    cpu_step();
    nvic_poll();           // проверка/доставка прерываний
    systick_tick();        // системный таймер
    peripherals_tick();    // обновление периферии
}
```

Режимы:
- step-by-step (отладка)
- run (быстрое исполнение)

---

## 8. Минимальный ISA (MVP)

Арифметика:
- ADD, SUB, ADC, SBC
- MOV, CMP

Логика:
- AND, ORR, EOR, BIC
- LSL, LSR, ASR

Переходы:
- B, BL, BX, CBZ/CBNZ

Память:
- LDR, STR (word)
- LDRB/STRB, LDRH/STRH (расширение)

Системные:
- NOP
- SVC (для входа в обработчик)

---

## 9. Прерывания (NVIC)

Модель:
- vector table по адресу 0x00000000
- приоритеты, pending/active биты
- вход в Handler mode

Поведение при входе:
- push: R0-R3, R12, LR, PC, xPSR
- загрузка PC из таблицы векторов

Возврат:
- специальное значение в LR (EXC_RETURN)
- pop контекста

---

## 10. Загрузка программы

Варианты:
- raw binary (MVP)
- ELF (расширение)

Инициализация:
```c
load_binary("program.bin", FLASH_BASE);
cpu.pc = read32(FLASH_BASE + 4);   // reset handler
cpu.sp = read32(FLASH_BASE + 0);   // initial SP
```

---

## 11. Тестирование

Unit tests:
- ALU операции
- отдельные инструкции

Integration tests:
- небольшие программы (loop, memcpy)
- обработка прерываний

Golden tests:
- сравнение ожидаемого состояния регистров/памяти

---

## 12. Технологии

- C (ядро симулятора)
- Go (опциональный сервис)
- VSCode
- Git (GitHub/GitLab)
- Markdown/Obsidian (база знаний)

---

## 13. Принципы

1. Архитектура важнее кода
2. MVP → расширение
3. Тесты обязательны

---

## 14. Roadmap

Этап 1: CPU MVP
- регистры, fetch/decode/execute
- ADD/SUB/MOV/B

Этап 2: Память и загрузка
- карта памяти, raw loader

Этап 3: Расширение ISA
- load/store, ветвления

Этап 4: Прерывания
- NVIC, vector table, SVC

Этап 5: Периферия
- SysTick, UART

Этап 6: Отладка
- трассировка, дампы

---

## 15. Memory Map

Базовые адреса (упрощённая модель):

- Flash:  0x08000000
- SRAM:   0x20000000
- MMIO:   0x40000000
- SCS:    0xE000E000

Ключевые периферийные блоки:

- NVIC:     0xE000E100
- SysTick:  0xE000E010
- UART1:    0x40013800
- TIM2:     0x40000000

Принцип:
- Все обращения CPU идут через bus
- bus маршрутизирует доступ по диапазону адреса
- периферия реализуется как MMIO-устройства

---

## 16. Ключевые знания

- CPU pipeline
- ISA
- Memory model
- Interrupts

---

## 17. API (черновик)

```c
typedef struct {
    uint32_t regs[16];
    uint32_t apsr;
} cpu_t;

uint8_t  bus_read8(uint32_t addr);
uint16_t bus_read16(uint32_t addr);
uint32_t bus_read32(uint32_t addr);

void bus_write8(uint32_t addr, uint8_t value);
void bus_write16(uint32_t addr, uint16_t value);
void bus_write32(uint32_t addr, uint32_t value);

void cpu_init(cpu_t*);
void cpu_step(cpu_t*);
```

## 18. Stop conditions

Симулятор должен завершаться при:

- достижении лимита инструкций
- выполнении команды выхода (например SVC)
- возникновении ошибки (fault)
- достижении breakpoint (если реализовано)

---

## 19. Наблюдаемость (логирование)
- trace: PC, opcode, operands
- регистры до/после
- события прерываний

Формат trace (пример):

PC=0x08000100 INST=0xF04F0301 R0=... R1=... APSR=...
