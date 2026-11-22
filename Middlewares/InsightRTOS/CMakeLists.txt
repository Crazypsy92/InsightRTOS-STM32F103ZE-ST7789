include(${CMAKE_CURRENT_LIST_DIR}/config/insight_project_config.cmake)
add_library(insightrtos_kernel STATIC
    kernel/deadlock.c kernel/trace.c kernel/queue.c lib/memcpy.c kernel/task.c kernel/scheduler.c
    kernel/ready_queue.c kernel/critical.c kernel/tick.c kernel/delay.c
    kernel/delay_queue.c kernel/semaphore.c kernel/mutex.c kernel/priority.c
    kernel/priority_graph.c kernel/wait.c arch/arm/cortex_m3/port.c
    arch/arm/cortex_m3/portasm.S arch/arm/cortex_m3/systick.c
    arch/arm/cortex_m3/systick_config.c)
target_include_directories(insightrtos_kernel PUBLIC include arch/arm/cortex_m3 PRIVATE kernel)
target_compile_definitions(insightrtos_kernel PUBLIC ${INSIGHT_PRODUCTION_DEFINITIONS})
target_compile_options(insightrtos_kernel PRIVATE
    -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -ffreestanding -fno-builtin -fno-common
    -ffunction-sections -fdata-sections -Wa,--noexecstack
    $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror;-Wpedantic;-Wshadow;-Wconversion;-Wstrict-prototypes;-Wmissing-prototypes;-Wundef;-Wformat=2;-fstack-usage>)
