add_subdirectory(Middlewares/InsightRTOS)
get_target_property(app_sources ${CMAKE_PROJECT_NAME} SOURCES)
list(FILTER app_sources EXCLUDE REGEX "/(delay|sysmem|syscalls)\\.c$")
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SOURCES "${app_sources}")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/rtos_smoke_app.c
    Middlewares/InsightRTOS/port/insight_board_port.c
    Middlewares/InsightRTOS/port/insight_timebase.c)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE Core Middlewares/InsightRTOS/port)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE INSIGHT_RTOS_ENABLED=1)
# Startup weak aliases alone do not extract portasm.S from a static archive.
# Load production objects, then let --gc-sections discard unused functions.
target_link_libraries(${CMAKE_PROJECT_NAME}
    -Wl,--whole-archive insightrtos_kernel -Wl,--no-whole-archive)
# LL utils is unused by smoke: original bare target still builds it unchanged.
# Its unused PLL helper parameters fail -Wextra; do not suppress diagnostics.
get_target_property(ll_sources STM32_Drivers SOURCES)
list(FILTER ll_sources EXCLUDE REGEX "/stm32f1xx_ll_utils[.]c$")
set_property(TARGET STM32_Drivers PROPERTY SOURCES "${ll_sources}")
foreach(target ${CMAKE_PROJECT_NAME} STM32_Drivers)
    target_compile_options(${target} PRIVATE -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
        $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror;-Wshadow;-Wstrict-prototypes;-Wmissing-prototypes;-Wundef;-Wformat=2>)
endforeach()
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -mthumb -mfloat-abi=soft
    -Wl,-z,noexecstack -Wl,--defsym=__insight_rtos__=1 -Wl,--undefined=_sbrk -Wl,--fatal-warnings)
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_SOURCE_DIR}/STM32F103xx_FLASH.ld)
