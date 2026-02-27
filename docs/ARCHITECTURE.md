# Архитектура проекта

---

# 1. Общая схема

+-------------------+
|   KeyDB Queue     |
+-------------------+
          |
          v
+-------------------+
|   Go Microservice |
|  - queue          |
|  - runner         |
|  - OTel           |
+-------------------+
          |
          v
+-------------------+
|   C Simulator     |
|  CPU + MMIO       |
+-------------------+

---

# 2. Архитектура симулятора (C)

## 2.1 Основные модули

/sim
  /src
    cpu/
      cpu.c
      decoder.c
      executor.c
    memory/
      memory.c
    mmio/
      bus.c
    devices/
      tim2.c
      usart1.c
      nvic.c
    sim.c

---

## 2.2 Главный цикл симуляции

while (running) {
    fetch();
    decode();
    execute();
    update_devices();
    check_interrupts();
}

---

## 2.3 MMIO Bus

uint32_t bus_read(uint32_t addr);
void bus_write(uint32_t addr, uint32_t value);

Если адрес:
- в SRAM → память
- в Flash → память (read-only)
- в диапазоне TIM2 → tim2_read/write
- в диапазоне USART1 → usart_read/write
- в NVIC → nvic_read/write

---

## 2.4 Интерфейс устройства

typedef struct {
    void (*tick)(void);
    uint32_t (*read)(uint32_t offset);
    void (*write)(uint32_t offset, uint32_t value);
} device_t;

---

# 3. Архитектура микросервиса (Go)

## 3.1 Структура

/service
  /internal
    queue/
    runner/
    model/
    otel/

---

## 3.2 Workflow

1) Получить job из KeyDB
2) Сохранить firmware во временный файл
3) Запустить симулятор
4) Дождаться завершения/таймаута
5) Опубликовать результат
6) Записать trace

---

# 4. Обработка ошибок

- Unsupported instruction → статус UNSUPPORTED
- HardFault → статус CRASH
- Timeout → статус TIMEOUT

---

# 5. Расширяемость

Будущие улучшения:
- GPIO модель
- DMA
- ELF загрузчик
- SysTick
- Pipeline модель CPU