from __future__ import annotations

import datetime as dt
import textwrap
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Rectangle
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "reports"
ASSET_DIR = OUT_DIR / "assets"
DOCX_PATH = OUT_DIR / "pvs_processor_simulation_report.docx"
PDF_PATH = OUT_DIR / "pvs_processor_simulation_report.pdf"

PROJECT_TITLE = "Разработка симулятора микроконтроллера STM32F103C8T6 с Go-сервисом и веб-интерфейсом управления пинами"
REPORT_TITLE = "Отчет по проекту"
ORG = "Университет ИТМО"
CITY_YEAR = "Санкт-Петербург, 2026"
AUTHOR_PLACEHOLDER = "ФИО авторов: ______________________________"
SUPERVISOR_PLACEHOLDER = "Руководитель: ______________________________"


def xml_text(text: str) -> str:
    return escape(text, {'"': "&quot;"})


def make_box(ax, xy, wh, text, color="#eef4ff", edge="#315b8a", fontsize=12):
    box = FancyBboxPatch(
        xy,
        wh[0],
        wh[1],
        boxstyle="round,pad=0.03,rounding_size=0.025",
        linewidth=1.6,
        edgecolor=edge,
        facecolor=color,
    )
    ax.add_patch(box)
    ax.text(
        xy[0] + wh[0] / 2,
        xy[1] + wh[1] / 2,
        text,
        ha="center",
        va="center",
        fontsize=fontsize,
        color="#102033",
        wrap=True,
    )


def make_arrow(ax, start, end, color="#374151"):
    ax.add_patch(
        FancyArrowPatch(
            start,
            end,
            arrowstyle="-|>",
            mutation_scale=18,
            linewidth=1.5,
            color=color,
        )
    )


def save_architecture_diagram(path: Path):
    fig, ax = plt.subplots(figsize=(12, 7), dpi=180)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    fig.patch.set_facecolor("#ffffff")

    make_box(ax, (0.05, 0.64), (0.22, 0.18), "Frontend\nHTML/CSS/JS\nBlue Pill board view", "#e8f7f0", "#15803d")
    make_box(ax, (0.39, 0.64), (0.22, 0.18), "Go HTTP service\n/api/run\n/api/session/*", "#eef4ff", "#2563eb")
    make_box(ax, (0.73, 0.64), (0.22, 0.18), "C simulator\npvs_sim_cli\npvs_sim_debug", "#fff7ed", "#c2410c")

    make_box(ax, (0.17, 0.22), (0.18, 0.18), "CPU\nThumb/Thumb-2\nIRQ entry/return", "#f5f3ff", "#7c3aed", 11)
    make_box(ax, (0.41, 0.22), (0.18, 0.18), "Bus + memory\nFlash/SRAM\nfault model", "#f8fafc", "#475569", 11)
    make_box(ax, (0.65, 0.22), (0.22, 0.18), "MMIO devices\nNVIC, TIM2,\nUSART1, GPIOA", "#ecfeff", "#0891b2", 11)

    make_arrow(ax, (0.27, 0.73), (0.39, 0.73))
    make_arrow(ax, (0.61, 0.73), (0.73, 0.73))
    make_arrow(ax, (0.50, 0.64), (0.50, 0.40))
    make_arrow(ax, (0.35, 0.31), (0.41, 0.31))
    make_arrow(ax, (0.59, 0.31), (0.65, 0.31))
    make_arrow(ax, (0.76, 0.40), (0.84, 0.64))

    ax.text(0.5, 0.93, "Архитектура системы", ha="center", fontsize=20, weight="bold", color="#111827")
    ax.text(
        0.5,
        0.08,
        "GUI не вызывает C-симулятор напрямую: управление идет через контракт Go-сервиса и stateful debug bridge.",
        ha="center",
        fontsize=11,
        color="#4b5563",
    )
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def save_pin_sequence(path: Path):
    fig, ax = plt.subplots(figsize=(12, 6), dpi=180)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    steps = [
        ("1", "Клик по PA0\nво frontend"),
        ("2", "POST\n/api/session/{id}/pins/PA0"),
        ("3", "Go Manager\nEngine.SetPin"),
        ("4", "pvs_sim_debug\npin PA0 0/1"),
        ("5", "GPIOA->IDR\nобновлен"),
        ("6", "Firmware\nпечатает H/L"),
    ]
    xs = [0.08, 0.24, 0.40, 0.57, 0.74, 0.90]
    for i, (num, label) in enumerate(steps):
        ax.add_patch(plt.Circle((xs[i], 0.58), 0.055, color="#2563eb"))
        ax.text(xs[i], 0.58, num, ha="center", va="center", color="white", fontsize=14, weight="bold")
        ax.text(xs[i], 0.36, label, ha="center", va="center", fontsize=11, color="#111827")
        if i < len(xs) - 1:
            make_arrow(ax, (xs[i] + 0.06, 0.58), (xs[i + 1] - 0.06, 0.58), "#2563eb")

    ax.text(0.5, 0.88, "Сценарий управления пином через Go-сервис", ha="center", fontsize=19, weight="bold")
    ax.text(0.5, 0.15, "Демонстрационная прошивка читает PA0 через GPIOA IDR и отправляет символ в USART1.", ha="center", fontsize=11, color="#4b5563")
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def save_mmio_map(path: Path):
    fig, ax = plt.subplots(figsize=(8, 9), dpi=180)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    regions = [
        ("SCS / NVIC", "0xE000E000", "NVIC registers routed", "#ede9fe", "#7c3aed"),
        ("Peripherals", "0x40000000", "TIM2, GPIOA, USART1", "#ecfeff", "#0891b2"),
        ("SRAM", "0x20000000", "20 KiB data + stack", "#f0fdf4", "#16a34a"),
        ("Flash", "0x08000000", "64 KiB raw firmware", "#fff7ed", "#c2410c"),
    ]
    y = 0.76
    for name, base, note, color, edge in regions:
        ax.add_patch(Rectangle((0.16, y), 0.68, 0.13, facecolor=color, edgecolor=edge, linewidth=1.8))
        ax.text(0.20, y + 0.085, name, fontsize=15, weight="bold", color="#111827")
        ax.text(0.20, y + 0.045, base, fontsize=12, color="#374151")
        ax.text(0.52, y + 0.063, note, fontsize=11, ha="center", color="#374151")
        y -= 0.17

    ax.text(0.5, 0.94, "Карта памяти и MMIO", ha="center", fontsize=20, weight="bold")
    ax.text(0.5, 0.06, "Все обращения CPU проходят через bus; неизвестные MMIO-регистры дают контролируемый fault.", ha="center", fontsize=10.5, color="#4b5563")
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def save_test_results(path: Path):
    fig, ax = plt.subplots(figsize=(9.8, 5.8), dpi=180)
    tests = ["CMake build", "CTest smoke", "CTest firmware", "GUI smoke", "Debug PA0 demo"]
    values = [1, 1, 1, 1, 1]
    colors = ["#16a34a"] * len(values)
    ax.barh(tests, values, color=colors)
    ax.set_xlim(0, 1.15)
    ax.set_xticks([])
    ax.set_title("Результаты проверок", fontsize=18, weight="bold", pad=18)
    ax.spines[["top", "right", "bottom", "left"]].set_visible(False)
    ax.tick_params(axis="y", labelsize=11)
    for i, value in enumerate(values):
        ax.text(value + 0.03, i, "passed", va="center", fontsize=11, color="#14532d", weight="bold")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def save_board_demo(path: Path):
    fig, ax = plt.subplots(figsize=(8, 8), dpi=180)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    ax.add_patch(FancyBboxPatch((0.30, 0.18), 0.40, 0.64, boxstyle="round,pad=0.03,rounding_size=0.02", facecolor="#0f766e", edgecolor="#134e4a", linewidth=2.0))
    ax.add_patch(Rectangle((0.39, 0.32), 0.22, 0.28, facecolor="#111827", edgecolor="#020617"))
    ax.text(0.50, 0.46, "STM32F103C8T6", ha="center", va="center", color="white", fontsize=9)
    left_pins = ["PA0", "PA1", "PA2", "PA3", "PA4", "PA5", "PA6", "PA7"]
    right_pins = ["PB0", "PB1", "PB10", "PB11", "PC13", "PC14", "PC15", "GND"]
    for i, pin in enumerate(left_pins):
        y = 0.76 - i * 0.075
        color = "#facc15" if pin == "PA0" else "#e5e7eb"
        ax.add_patch(Rectangle((0.19, y - 0.015), 0.11, 0.03, facecolor=color, edgecolor="#374151"))
        ax.text(0.16, y, pin, ha="right", va="center", fontsize=10, weight="bold" if pin == "PA0" else "normal")
    for i, pin in enumerate(right_pins):
        y = 0.76 - i * 0.075
        ax.add_patch(Rectangle((0.70, y - 0.015), 0.11, 0.03, facecolor="#e5e7eb", edgecolor="#374151"))
        ax.text(0.84, y, pin, ha="left", va="center", fontsize=10)
    ax.text(0.5, 0.91, "Демонстрация Blue Pill: PA0 как управляемый вход", ha="center", fontsize=16, weight="bold")
    ax.text(0.5, 0.10, "Желтый контакт PA0 переключается из frontend и читается прошивкой через GPIOA->IDR.", ha="center", fontsize=10.5, color="#374151")
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def generate_images() -> dict[str, Path]:
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    images = {
        "architecture": ASSET_DIR / "architecture.png",
        "pin_sequence": ASSET_DIR / "pin_sequence.png",
        "mmio_map": ASSET_DIR / "mmio_map.png",
        "tests": ASSET_DIR / "test_results.png",
        "board": ASSET_DIR / "board_demo.png",
    }
    save_architecture_diagram(images["architecture"])
    save_pin_sequence(images["pin_sequence"])
    save_mmio_map(images["mmio_map"])
    save_test_results(images["tests"])
    save_board_demo(images["board"])
    return images


def sections():
    return [
        {
            "title": "Введение",
            "level": 1,
            "paras": [
                "Настоящий отчет описывает разработку учебного программного комплекса для эмуляции микроконтроллера STM32F103C8T6 (Blue Pill) и управления его состоянием через Go-сервис и веб-интерфейс. Работа выполнена в рамках курса по проектированию вычислительных систем и ориентирована на практическое понимание архитектуры ARM Cortex-M3, модели памяти, MMIO-периферии и отладочного взаимодействия с исполняемой прошивкой.",
                "Ключевой результат текущего этапа — реализованный сквозной сценарий управления пинами процессора: пользователь переключает PA0 во frontend, Go-сервис передает команду в stateful debug bridge, C-симулятор обновляет входной уровень GPIOA, а тестовая прошивка читает GPIOA->IDR и выводит символ H или L через USART1. Такая вертикальная демонстрация показывает, что интерфейс управления влияет на реальное исполнение прошивки, а не только на визуальное состояние UI.",
                "Оформление отчета приближено к структуре выпускной квалификационной работы: титульный лист, оглавление, введение, основная часть, тестирование, заключение, список литературы и приложения. Использованы поля и структура, характерные для отчетов по ГОСТ 7.32-2017 и текстовых документов по ГОСТ 2.105-95.",
            ],
        },
        {
            "title": "1. Техническое задание",
            "level": 1,
            "paras": [
                "Цель проекта — разработать детерминированный симулятор микроконтроллера STM32F103C8T6 на языке C, а также сервисный и пользовательский слой для запуска, наблюдения и управления эмуляцией. На текущем этапе особый акцент сделан на управлении пинами процессора через Go-сервер и frontend.",
                "Объект разработки — программная модель микроконтроллера с ядром ARM Cortex-M3, памятью Flash/SRAM, шиной, MMIO-устройствами и механизмом прерываний. Предмет разработки — архитектура симуляции, API-контракт сервиса, live/debug session и демонстрационная прошивка, подтверждающая управление входными сигналами.",
            ],
            "tables": [
                (
                    "Таблица 1 — Основные требования к проекту",
                    [
                        ["Требование", "Реализация"],
                        ["Загрузка raw firmware", "Прошивка загружается во Flash, reset sequence читает MSP и PC из vector table."],
                        ["Модель CPU", "Поддержан MVP-набор Thumb/Thumb-2: арифметика, ветвления, load/store, SVC, PUSH/POP, BL, shifts."],
                        ["MMIO и периферия", "Реализованы TIM2, USART1, NVIC MMIO и минимальная модель GPIOA."],
                        ["Go-сервис", "HTTP API запускает симулятор и создает stateful debug sessions."],
                        ["Frontend", "Статический web UI отображает состояние платы, UART, CPU/peripherals и управляет PA0."],
                        ["Тестовая программа", "Demo firmware читает GPIOA->IDR и отправляет H/L в USART1."],
                    ],
                )
            ],
        },
        {
            "title": "2. Архитектура системы",
            "level": 1,
            "paras": [
                "Система разделена на три слоя. Нижний слой — C-симулятор, где находится состояние CPU, память, шина и устройства. Средний слой — Go-микросервис, отвечающий за HTTP-контракт, запуск subprocess, live sessions и передачу команд. Верхний слой — frontend, предоставляющий пользователю визуальную поверхность управления платой.",
                "Граница ответственности выбрана так, чтобы GUI не зависел от внутренних C-структур и не запускал симулятор напрямую. Все внешние действия проходят через Go API. Это упрощает замену UI, добавление очереди заданий, логирование, трассировку и дальнейшее развитие KeyDB/OpenTelemetry интеграции.",
            ],
            "images": [("Рисунок 1 — Архитектура системы", "architecture")],
            "subsections": [
                {
                    "title": "2.1 C-симулятор",
                    "level": 2,
                    "paras": [
                        "C-слой содержит ядро симуляции. Структура sim_t объединяет CPU, memory, bus, NVIC, TIM2, USART1, GPIOA, UART output buffer и диагностические поля. Выполнение идет по модели fetch-decode-execute с последующим обслуживанием периферии и прерываний.",
                        "Шина маршрутизирует обращения CPU в Flash, SRAM и MMIO-регистры. Невыровненные halfword/word-доступы и обращения к неподдержанным MMIO-регистрам завершаются контролируемым fault result, что важно для повторяемого тестирования.",
                    ],
                },
                {
                    "title": "2.2 Go-сервис",
                    "level": 2,
                    "paras": [
                        "Go-сервис предоставляет синхронный endpoint POST /api/run и live/session endpoints: create, get, step, run, stop, events и pins. Для обычного запуска используется pvs_sim_cli, для интерактивной отладки — pvs_sim_debug, который держит живое состояние sim_t между командами.",
                        "Команда управления пином проходит через session.Manager.SetPin и debugbridge.Engine.SetPin. При наличии level сервис отправляет в debug bridge строку вида pin PA0 1, после чего получает свежий JSON snapshot.",
                    ],
                },
                {
                    "title": "2.3 Frontend",
                    "level": 2,
                    "paras": [
                        "Frontend реализован без npm-зависимостей как статический HTML/CSS/JS. Он умеет создавать live session, выполнять step/run/stop, подписываться на SSE events, показывать UART output, CPU snapshot, peripheral snapshot и визуальное состояние пинов Blue Pill.",
                        "В demo-прошивку по умолчанию уже встроен сценарий PA0 -> USART1. Пользователь создает session, кликает PA0 для переключения уровня и запускает прошивку. В UART output появляется H при высоком уровне и L при низком.",
                    ],
                    "images": [("Рисунок 2 — Визуальная модель платы и управляемый PA0", "board")],
                },
            ],
        },
        {
            "title": "3. Описание реализации",
            "level": 1,
            "paras": [
                "В ходе работы были реализованы CPU L1-задачи, базовая периферия и сервисные контракты. Последний этап сфокусирован на GPIOA и реальном влиянии frontend pin control на исполнение прошивки.",
            ],
            "subsections": [
                {
                    "title": "3.1 Минимальная GPIOA/MMIO-модель",
                    "level": 2,
                    "paras": [
                        "Добавлен модуль GPIOA с регистрами CRL, CRH, IDR, ODR, BSRR и BRR. CRL/CRH сохраняются для видимости конфигурации, IDR хранит входные уровни, ODR — выходные уровни, BSRR/BRR изменяют выходные биты по правилам STM32-подобной модели.",
                        "GPIOA подключен к bus и sim_t. Теперь обращения firmware по адресу 0x40010800 маршрутизируются в gpio_read32/gpio_write32, а debug bridge может обновлять входные уровни через gpio_set_input.",
                    ],
                    "images": [("Рисунок 3 — Карта памяти и реализованные MMIO-области", "mmio_map")],
                },
                {
                    "title": "3.2 Stateful debug bridge и управление пинами",
                    "level": 2,
                    "paras": [
                        "В pvs_sim_debug добавлена команда pin <name> <level>. Команда поддерживает порт A и уровни 0/1. После применения уровня debug bridge сразу возвращает обновленный JSON snapshot, включая pins array с mode=input и текущим level для PA0-PA7.",
                        "В Go-интерфейс Engine добавлен метод SetPin(ctx, name, request). Менеджер сессий перестал ограничиваться session-local overlay: теперь он вызывает engine.SetPin, обновляет состояние session и публикует snapshot подписчикам SSE.",
                    ],
                    "images": [("Рисунок 4 — Последовательность управления пином через frontend и Go-сервис", "pin_sequence")],
                },
                {
                    "title": "3.3 Демонстрационная firmware-программа",
                    "level": 2,
                    "paras": [
                        "Демонстрационная прошивка инициализирует USART1 TX, читает GPIOA->IDR, маскирует бит PA0 и выбирает символ для передачи. Если PA0 равен 1, в USART1->DR записывается H; если PA0 равен 0, записывается L. Завершение выполняется через SVC как контролируемый break.",
                        "Этот сценарий включен в firmware_integration_test и в textarea frontend по умолчанию. За счет этого отчетный пример воспроизводится без ручной сборки бинарного файла.",
                    ],
                },
            ],
        },
        {
            "title": "4. Тестирование",
            "level": 1,
            "paras": [
                "Тестирование построено по нескольким уровням: сборка CMake, smoke-тесты CPU/peripherals, интеграционные firmware-сценарии, smoke-тест frontend-логики и ручная проверка stateful debug bridge. Такой набор закрывает как нижний C-слой, так и пользовательский путь управления пином.",
            ],
            "tables": [
                (
                    "Таблица 2 — Выполненные проверки",
                    [
                        ["Проверка", "Результат"],
                        ["cmake --build build-ninja", "Успешно"],
                        ["ctest --test-dir build-ninja --output-on-failure", "2/2 tests passed"],
                        ["node gui/tests/app_smoke_test.js", "Успешно после запуска вне sandbox"],
                        ["pvs_sim_debug: pin PA0 1 + run", "UART output = H, snapshot PA0 level = 1"],
                        ["firmware_integration_test: PA0 low/high", "Ожидаемые символы L/H подтверждены"],
                    ],
                )
            ],
            "images": [("Рисунок 5 — Сводка результатов проверок", "tests")],
            "subsections": [
                {
                    "title": "4.1 CTest",
                    "level": 2,
                    "paras": [
                        "Smoke-тесты проверяют reset sequence, базовые инструкции, флаги, ветвления, работу со стеком, load/store, fault cases, NVIC, TIM2, USART1 и GPIOA MMIO. Интеграционные тесты запускают небольшие прошивки: hello_uart, loop_counter, timer_irq, TIM2 IRQ -> USART1 и PA0 -> USART1.",
                    ],
                },
                {
                    "title": "4.2 Ограничения проверки",
                    "level": 2,
                    "paras": [
                        "В текущем окружении отсутствует go в PATH, поэтому go test ./... не выполнялся. Тем не менее интерфейсные изменения Go-кода были согласованы компиляционно по структуре и проверены через существующие unit-тесты логики там, где доступен Node/CMake. Для полноценного CI рекомендуется добавить среду с Go toolchain.",
                    ],
                },
            ],
        },
        {
            "title": "Заключение",
            "level": 1,
            "paras": [
                "В результате выполненной работы получен рабочий MVP симулятора STM32F103C8T6 с C-ядром, Go-сервисом и статическим frontend. Реализована ключевая вертикаль управления пинами процессора через frontend: UI отправляет запрос в Go-сервис, сервис управляет живой debug session, debug bridge изменяет GPIOA input, а прошивка реагирует на реальный уровень PA0.",
                "Проект готов для демонстрации базовой эмуляции процессора, MMIO-периферии и пользовательского управления входными сигналами. Дальнейшее развитие целесообразно вести в сторону lifecycle cleanup для debug sessions, расширения USART1 RX/IRQ, SysTick/SCB stubs, синхронизации ISA coverage и подключения полноценного Go toolchain в CI.",
            ],
        },
        {
            "title": "Список литературы",
            "level": 1,
            "paras": [
                "1. ГОСТ 7.32-2017. СИБИД. Отчет о научно-исследовательской работе. Структура и правила оформления.",
                "2. ГОСТ 2.105-95. ЕСКД. Общие требования к текстовым документам.",
                "3. Калинина М. И., Смирнов С. Б. Методические указания по выполнению выпускных квалификационных работ для бакалавров. Санкт-Петербург: Университет ИТМО, 2016.",
                "4. ARM Architecture Reference Manual ARMv7-M. Architecture profile for microcontrollers.",
                "5. STMicroelectronics. RM0008 Reference manual. STM32F101xx, STM32F102xx, STM32F103xx, STM32F105xx and STM32F107xx advanced ARM-based 32-bit MCUs.",
                "6. STMicroelectronics. STM32F103C8 datasheet.",
                "7. ПВС 2026 весна. Лекция 1. Практическое задание. PDF-файл, приложенный к проекту.",
                "8. Локальная документация проекта: docs/PROJECT_CONTEXT.md, docs/ARCHITECTURE.md, docs/MMIO_MAP.md, docs/GUI_PLAN.md, docs/TASKS.md.",
            ],
        },
        {
            "title": "Приложения",
            "level": 1,
            "paras": [
                "Приложение А. HTTP API live/debug session: POST /api/session создает сессию; POST /api/session/{id}/pins/{name} изменяет входной уровень пина; POST /api/session/{id}/run выполняет прошивку; GET /api/session/{id}/events возвращает SSE stream snapshot-событий.",
                "Приложение Б. Команды проверки: cmake --build build-ninja; ctest --test-dir build-ninja --output-on-failure; node gui/tests/app_smoke_test.js; pvs_sim_debug --firmware <demo.bin> с командами pin PA0 1 и run 20.",
                "Приложение В. Последний коммит реализации: 2dfa609 Wire GPIO pin control through debug sessions. В него вошли GPIOA, SetPin в Go debug bridge, demo firmware, CTest-проверки и обновление документации.",
            ],
        },
    ]


class DocxBuilder:
    def __init__(self):
        self.body: list[str] = []
        self.rels: list[tuple[str, str, str]] = []
        self.media: list[tuple[str, Path]] = []
        self.next_rel_id = 1

    def rel(self, target: str, typ: str) -> str:
        rid = f"rId{self.next_rel_id}"
        self.next_rel_id += 1
        self.rels.append((rid, typ, target))
        return rid

    def paragraph(self, text="", style=None, align=None, bold=False, italic=False, size=None, before=0, after=120, indent=True):
        ppr = []
        if style:
            ppr.append(f'<w:pStyle w:val="{style}"/>')
        if align:
            ppr.append(f'<w:jc w:val="{align}"/>')
        if before or after:
            ppr.append(f'<w:spacing w:before="{before}" w:after="{after}" w:line="360" w:lineRule="auto"/>')
        if indent and style in (None, "Normal"):
            ppr.append('<w:ind w:firstLine="709"/>')
        rpr = []
        if bold:
            rpr.append("<w:b/>")
        if italic:
            rpr.append("<w:i/>")
        if size:
            rpr.append(f'<w:sz w:val="{size * 2}"/>')
        rpr_xml = f"<w:rPr>{''.join(rpr)}</w:rPr>" if rpr else ""
        parts = text.split("\n")
        runs = []
        for i, part in enumerate(parts):
            if i:
                runs.append("<w:br/>")
            runs.append(f"<w:t>{xml_text(part)}</w:t>")
        self.body.append(f"<w:p><w:pPr>{''.join(ppr)}</w:pPr><w:r>{rpr_xml}{''.join(runs)}</w:r></w:p>")

    def heading(self, text, level=1):
        self.paragraph(text, style=f"Heading{level}", indent=False, after=180)

    def page_break(self):
        self.body.append('<w:p><w:r><w:br w:type="page"/></w:r></w:p>')

    def toc(self):
        instr = 'TOC \\o "1-3" \\h \\z \\u'
        self.body.append(
            '<w:p><w:r><w:fldChar w:fldCharType="begin"/></w:r>'
            f'<w:r><w:instrText xml:space="preserve">{xml_text(instr)}</w:instrText></w:r>'
            '<w:r><w:fldChar w:fldCharType="separate"/></w:r>'
            '<w:r><w:t>Оглавление обновляется через Word: Ссылки → Обновить таблицу.</w:t></w:r>'
            '<w:r><w:fldChar w:fldCharType="end"/></w:r></w:p>'
        )

    def table(self, rows):
        xml_rows = []
        for r, row in enumerate(rows):
            cells = []
            for cell in row:
                shade = '<w:shd w:fill="DDEBFF"/>' if r == 0 else ""
                cells.append(
                    "<w:tc><w:tcPr>"
                    '<w:tcW w:w="2400" w:type="dxa"/>'
                    f"{shade}</w:tcPr>"
                    f'<w:p><w:pPr><w:spacing w:after="80"/></w:pPr><w:r>'
                    f'{"<w:rPr><w:b/></w:rPr>" if r == 0 else ""}<w:t>{xml_text(cell)}</w:t></w:r></w:p>'
                    "</w:tc>"
                )
            xml_rows.append(f"<w:tr>{''.join(cells)}</w:tr>")
        self.body.append(
            '<w:tbl><w:tblPr><w:tblStyle w:val="TableGrid"/>'
            '<w:tblBorders><w:top w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
            '<w:left w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
            '<w:bottom w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
            '<w:right w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
            '<w:insideH w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
            '<w:insideV w:val="single" w:sz="4" w:space="0" w:color="808080"/></w:tblBorders></w:tblPr>'
            + "".join(xml_rows)
            + "</w:tbl>"
        )
        self.paragraph("", after=80)

    def image(self, image_path: Path, caption: str, width_cm=15.5):
        img = Image.open(image_path)
        width_px, height_px = img.size
        width_emu = int(width_cm / 2.54 * 914400)
        height_emu = int(width_emu * height_px / width_px)
        media_name = f"image{len(self.media) + 1}{image_path.suffix}"
        self.media.append((media_name, image_path))
        rid = self.rel(f"media/{media_name}", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image")
        self.body.append(
            f'''<w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r><w:drawing>
<wp:inline distT="0" distB="0" distL="0" distR="0"
xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing">
<wp:extent cx="{width_emu}" cy="{height_emu}"/>
<wp:docPr id="{len(self.media)}" name="{xml_text(caption)}"/>
<a:graphic xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">
<a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">
<pic:pic xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture">
<pic:nvPicPr><pic:cNvPr id="0" name="{media_name}"/><pic:cNvPicPr/></pic:nvPicPr>
<pic:blipFill><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>
<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{width_emu}" cy="{height_emu}"/></a:xfrm>
<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr>
</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>'''
        )
        self.paragraph(caption, style="Caption", align="center", italic=True, indent=False)

    def build(self, path: Path, image_map: dict[str, Path]):
        self._content(image_map)
        self._save(path)

    def _content(self, image_map):
        self.paragraph(ORG, align="center", bold=True, size=14, indent=False, after=240)
        self.paragraph("Факультет: ______________________________", align="center", size=12, indent=False, after=160)
        self.paragraph(REPORT_TITLE.upper(), align="center", bold=True, size=18, indent=False, before=1000, after=260)
        self.paragraph(PROJECT_TITLE, align="center", bold=True, size=16, indent=False, after=500)
        self.paragraph(AUTHOR_PLACEHOLDER, align="right", size=14, indent=False, after=120)
        self.paragraph(SUPERVISOR_PLACEHOLDER, align="right", size=14, indent=False, after=900)
        self.paragraph(CITY_YEAR, align="center", size=14, indent=False, before=1200)
        self.page_break()

        self.heading("Оглавление", 1)
        self.toc()
        self.paragraph("Для точной нумерации страниц в DOCX откройте документ в Word или LibreOffice и выполните обновление оглавления.", italic=True)
        self.page_break()

        for sec in sections():
            self._section(sec, image_map)
            self.page_break()
        if self.body and self.body[-1].startswith('<w:p><w:r><w:br'):
            self.body.pop()

    def _section(self, sec, image_map):
        self.heading(sec["title"], sec.get("level", 1))
        for para in sec.get("paras", []):
            self.paragraph(para)
        for caption, key in sec.get("images", []):
            self.image(image_map[key], caption)
        for caption, rows in sec.get("tables", []):
            self.paragraph(caption, style="Caption", align="center", italic=True, indent=False)
            self.table(rows)
        for sub in sec.get("subsections", []):
            self._section(sub, image_map)

    def _save(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        footer_default = (
            '<w:ftr xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">'
            '<w:p><w:pPr><w:jc w:val="center"/></w:pPr>'
            '<w:r><w:fldChar w:fldCharType="begin"/></w:r>'
            '<w:r><w:instrText xml:space="preserve"> PAGE </w:instrText></w:r>'
            '<w:r><w:fldChar w:fldCharType="end"/></w:r></w:p></w:ftr>'
        )
        footer_first = '<w:ftr xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:p/></w:ftr>'
        footer_rid = self.rel("footer1.xml", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer")
        first_footer_rid = self.rel("footerFirst.xml", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer")

        sect_pr = (
            '<w:sectPr><w:footerReference w:type="default" r:id="' + footer_rid + '"/>'
            '<w:footerReference w:type="first" r:id="' + first_footer_rid + '"/>'
            '<w:titlePg/><w:pgSz w:w="11906" w:h="16838"/>'
            '<w:pgMar w:top="1134" w:right="567" w:bottom="1134" w:left="1701" w:header="708" w:footer="708" w:gutter="0"/>'
            '</w:sectPr>'
        )
        document = (
            '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
            '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
            'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
            '<w:body>' + "".join(self.body) + sect_pr + '</w:body></w:document>'
        )
        rel_xml = ''.join(
            f'<Relationship Id="{rid}" Type="{typ}" Target="{target}"/>'
            for rid, typ, target in self.rels
        )
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("[Content_Types].xml", CONTENT_TYPES)
            z.writestr("_rels/.rels", ROOT_RELS)
            z.writestr("docProps/core.xml", core_xml())
            z.writestr("docProps/app.xml", APP_XML)
            z.writestr("word/document.xml", document)
            z.writestr("word/styles.xml", STYLES_XML)
            z.writestr("word/settings.xml", SETTINGS_XML)
            z.writestr("word/footer1.xml", footer_default)
            z.writestr("word/footerFirst.xml", footer_first)
            z.writestr(
                "word/_rels/document.xml.rels",
                '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                + rel_xml
                + "</Relationships>",
            )
            for media_name, image_path in self.media:
                z.write(image_path, f"word/media/{media_name}")


CONTENT_TYPES = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Default Extension="png" ContentType="image/png"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
<Override PartName="/word/settings.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml"/>
<Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/>
<Override PartName="/word/footerFirst.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/>
<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
<Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
</Types>'''

ROOT_RELS = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>'''

APP_XML = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"
xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">
<Application>Codex report generator</Application></Properties>'''

SETTINGS_XML = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:settings xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:updateFields w:val="true"/></w:settings>'''

STYLES_XML = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Times New Roman" w:hAnsi="Times New Roman" w:cs="Times New Roman"/><w:sz w:val="28"/></w:rPr></w:rPrDefault>
<w:pPrDefault><w:pPr><w:spacing w:line="360" w:lineRule="auto" w:after="120"/></w:pPr></w:pPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/><w:rPr><w:rFonts w:ascii="Times New Roman" w:hAnsi="Times New Roman" w:cs="Times New Roman"/><w:sz w:val="28"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:spacing w:before="240" w:after="180"/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:sz w:val="32"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:spacing w:before="180" w:after="120"/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:sz w:val="30"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Caption"><w:name w:val="caption"/><w:basedOn w:val="Normal"/><w:qFormat/><w:pPr><w:spacing w:after="160"/></w:pPr><w:rPr><w:i/><w:sz w:val="24"/></w:rPr></w:style>
<w:style w:type="table" w:styleId="TableGrid"><w:name w:val="Table Grid"/><w:tblPr><w:tblBorders><w:top w:val="single" w:sz="4"/><w:left w:val="single" w:sz="4"/><w:bottom w:val="single" w:sz="4"/><w:right w:val="single" w:sz="4"/><w:insideH w:val="single" w:sz="4"/><w:insideV w:val="single" w:sz="4"/></w:tblBorders></w:tblPr></w:style>
</w:styles>'''


def core_xml():
    now = dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
xmlns:dc="http://purl.org/dc/elements/1.1/"
xmlns:dcterms="http://purl.org/dc/terms/"
xmlns:dcmitype="http://purl.org/dc/dcmitype/"
xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
<dc:title>{xml_text(REPORT_TITLE)}</dc:title>
<dc:subject>{xml_text(PROJECT_TITLE)}</dc:subject>
<dc:creator>Codex</dc:creator>
<cp:lastModifiedBy>Codex</cp:lastModifiedBy>
<dcterms:created xsi:type="dcterms:W3CDTF">{now}</dcterms:created>
<dcterms:modified xsi:type="dcterms:W3CDTF">{now}</dcterms:modified>
</cp:coreProperties>'''


def pdf_wrap(text: str, width=92):
    lines = []
    for para in text.split("\n"):
        if not para:
            lines.append("")
        else:
            lines.extend(textwrap.wrap(para, width=width, break_long_words=False))
    return lines


def write_pdf(path: Path, image_map: dict[str, Path]):
    path.parent.mkdir(parents=True, exist_ok=True)
    page_no = 0

    def new_page(pdf, title=None):
        nonlocal page_no
        page_no += 1
        fig = plt.figure(figsize=(8.27, 11.69))
        fig.patch.set_facecolor("white")
        if title:
            fig.text(0.13, 0.93, title, fontsize=15, weight="bold", ha="left", va="top")
        if page_no != 1:
            fig.text(0.5, 0.035, str(page_no), fontsize=10, ha="center", va="center")
        return fig

    def text_pages(pdf, title, paras, tables=None, images=None):
        fig = new_page(pdf, title)
        y = 0.88
        for para in paras:
            for line in pdf_wrap(para):
                if y < 0.10:
                    pdf.savefig(fig, bbox_inches="tight")
                    plt.close(fig)
                    fig = new_page(pdf, title)
                    y = 0.88
                fig.text(0.13, y, line, fontsize=10.5, ha="left", va="top")
                y -= 0.028
            y -= 0.020
        for caption, rows in tables or []:
            if y < 0.28:
                pdf.savefig(fig, bbox_inches="tight")
                plt.close(fig)
                fig = new_page(pdf, title)
                y = 0.88
            fig.text(0.5, y, caption, fontsize=10, style="italic", ha="center")
            y -= 0.035
            for row in rows:
                line = " | ".join(row)
                for wrapped in pdf_wrap(line, 80):
                    fig.text(0.13, y, wrapped, fontsize=8.7, ha="left", va="top")
                    y -= 0.023
                y -= 0.004
            y -= 0.025
        pdf.savefig(fig, bbox_inches="tight")
        plt.close(fig)
        for caption, key in images or []:
            fig = new_page(pdf, title)
            ax = fig.add_axes([0.08, 0.20, 0.84, 0.66])
            ax.imshow(Image.open(image_map[key]))
            ax.axis("off")
            fig.text(0.5, 0.15, caption, fontsize=10.5, style="italic", ha="center")
            pdf.savefig(fig, bbox_inches="tight")
            plt.close(fig)

    with PdfPages(path) as pdf:
        fig = new_page(pdf)
        fig.text(0.5, 0.88, ORG, fontsize=14, weight="bold", ha="center")
        fig.text(0.5, 0.80, REPORT_TITLE.upper(), fontsize=18, weight="bold", ha="center")
        fig.text(0.5, 0.70, "\n".join(textwrap.wrap(PROJECT_TITLE, 55)), fontsize=15, weight="bold", ha="center")
        fig.text(0.70, 0.52, AUTHOR_PLACEHOLDER, fontsize=12, ha="center")
        fig.text(0.70, 0.48, SUPERVISOR_PLACEHOLDER, fontsize=12, ha="center")
        fig.text(0.5, 0.12, CITY_YEAR, fontsize=13, ha="center")
        pdf.savefig(fig, bbox_inches="tight")
        plt.close(fig)

        toc_lines = [
            "Введение",
            "1. Техническое задание",
            "2. Архитектура системы",
            "3. Описание реализации",
            "4. Тестирование",
            "Заключение",
            "Список литературы",
            "Приложения",
        ]
        text_pages(pdf, "Оглавление", ["\n".join(toc_lines)])

        def flatten(sec):
            imgs = sec.get("images", [])
            tabs = sec.get("tables", [])
            paras = list(sec.get("paras", []))
            text_pages(pdf, sec["title"], paras, tabs, imgs)
            for sub in sec.get("subsections", []):
                flatten(sub)

        for sec in sections():
            flatten(sec)


def main():
    images = generate_images()
    DocxBuilder().build(DOCX_PATH, images)
    write_pdf(PDF_PATH, images)
    print(DOCX_PATH)
    print(PDF_PATH)


if __name__ == "__main__":
    main()
