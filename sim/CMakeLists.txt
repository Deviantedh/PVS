set(SIM_PUBLIC_HEADERS
    include/sim/bus.h
    include/sim/cpu.h
    include/sim/irq.h
    include/sim/memory.h
    include/sim/nvic.h
    include/sim/sim.h
    include/sim/tim2.h
    include/sim/usart1.h
)

set(SIM_SOURCES
    src/bus.c
    src/memory.c
    src/sim.c
    src/cpu/cpu.c
    src/devices/nvic.c
    src/devices/tim2.c
    src/devices/usart1.c
    src/devices/stub.c
    src/util/log.c
)

add_library(pvs_sim STATIC ${SIM_PUBLIC_HEADERS} ${SIM_SOURCES})

target_include_directories(pvs_sim
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(pvs_sim
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(pvs_sim_cli src/cli.c)
target_link_libraries(pvs_sim_cli PRIVATE pvs_sim)

if(PVS_BUILD_TESTS)
    add_executable(pvs_sim_smoke_test tests/smoke_test.c)
    target_link_libraries(pvs_sim_smoke_test PRIVATE pvs_sim)

    add_executable(pvs_sim_firmware_integration_test tests/firmware_integration_test.c)
    target_link_libraries(pvs_sim_firmware_integration_test PRIVATE pvs_sim)

    add_test(NAME pvs_sim_smoke_test COMMAND pvs_sim_smoke_test)
    add_test(NAME pvs_sim_firmware_integration_test COMMAND pvs_sim_firmware_integration_test)
endif()
