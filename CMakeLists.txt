cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
option(INSIGHT_DISPLAY_RTOS "Build the original four-page RTOS application" OFF)
option(INSIGHT_RTOS_ENABLED "Build the isolated dual-task RTOS smoke image" OFF)
option(INSIGHT_TRACE_ENABLED "Enable kernel trace" OFF)
option(INSIGHT_DIAG_EXPLAIN "Enable explainable scheduling" OFF)
option(INSIGHT_DIAG_DL "Enable mutex deadlock diagnostics" OFF)
option(INSIGHT_DIAG_RT "Enable real-time timing diagnostics" OFF)
option(INSIGHT_TEST_STRESS "Enable controlled stress test injection in TIM2 ISR" OFF)
option(INSIGHT_TEST_TIMING "Enable target-only DWT timing benchmark" OFF)
option(INSIGHT_TEST_MSP "Enable target-only MSP watermark fixture" OFF)
if(INSIGHT_TEST_MSP AND (NOT INSIGHT_RTOS_ENABLED OR NOT INSIGHT_DISPLAY_RTOS OR
                        NOT INSIGHT_DIAG_RT OR NOT INSIGHT_TEST_STRESS OR
                        INSIGHT_TEST_TIMING OR INSIGHT_TEST_JOB))
    message(FATAL_ERROR "MSP measurement requires display RT with stress and excludes other fixtures")
endif()
if(INSIGHT_TEST_TIMING AND (NOT INSIGHT_RTOS_ENABLED OR NOT INSIGHT_DISPLAY_RTOS OR
                           NOT INSIGHT_TEST_STRESS OR INSIGHT_TEST_JOB))
    message(FATAL_ERROR "Timing benchmark requires the display stress profile without JobTest")
endif()
if(INSIGHT_RTOS_ENABLED)
    if(INSIGHT_DISPLAY_RTOS)
        set(CMAKE_PROJECT_NAME insightrtos_display)
    else()
        set(CMAKE_PROJECT_NAME insightrtos_rtos)
    endif()
else()
    set(CMAKE_PROJECT_NAME insightrtos)
endif()

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/key_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/multi_button.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/delay.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/spi_stm32_ll.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/bsp_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/st7789.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/st7789_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/lcd_fonts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/lcd_cn16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/app_lcd_test.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    $<$<BOOL:${INSIGHT_TEST_STRESS}>:INSIGHT_TEST_STRESS=1>
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

if(INSIGHT_RTOS_ENABLED)
    include(cmake/insight_rtos.cmake)
endif()
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    VERBATIM)

if(INSIGHT_DISPLAY_RTOS)
    include(cmake/insight_app.cmake)
endif()

option(INSIGHT_TEST_JOB "Enable one-shot target Job diagnostic fixture" OFF)
if(INSIGHT_TEST_JOB)
    if(NOT INSIGHT_DISPLAY_RTOS OR NOT INSIGHT_RTOS_ENABLED OR NOT INSIGHT_DIAG_RT OR INSIGHT_TEST_STRESS)
        message(FATAL_ERROR "Job fixture requires display RT and excludes stress injection")
    endif()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE tests/hardware/job_fixture.c)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE tests/hardware)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE INSIGHT_TEST_JOB=1)
endif()


if(INSIGHT_TEST_TIMING)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE tests/hardware/timing_bench.c)
    foreach(target ${CMAKE_PROJECT_NAME} insightrtos_kernel)
        target_compile_definitions(${target} PRIVATE INSIGHT_TEST_TIMING=1)
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/hardware)
    endforeach()
endif()

if(INSIGHT_TEST_MSP)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE tests/hardware/msp_bench.c)
    foreach(target ${CMAKE_PROJECT_NAME} insightrtos_kernel)
        target_compile_definitions(${target} PRIVATE INSIGHT_TEST_MSP=1)
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/hardware)
    endforeach()
endif()


option(INSIGHT_APP_INTEGRATED_DIAGNOSTICS "Export combined display diagnostics in Monitor" OFF)
if(INSIGHT_APP_INTEGRATED_DIAGNOSTICS)
    if(NOT INSIGHT_DISPLAY_RTOS OR NOT INSIGHT_RTOS_ENABLED OR NOT INSIGHT_TRACE_ENABLED OR
       NOT INSIGHT_DIAG_EXPLAIN OR NOT INSIGHT_DIAG_RT OR NOT INSIGHT_DIAG_DL OR
       INSIGHT_TEST_MSP OR INSIGHT_TEST_TIMING OR INSIGHT_TEST_JOB)
        message(FATAL_ERROR "Integrated diagnostics require display, Trace/Explain, RT and DL without other fixtures")
    endif()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Src/app_diagnostics.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE INSIGHT_APP_INTEGRATED_DIAGNOSTICS=1)
endif()

option(INSIGHT_TEST_MUTEX "Build isolated Cortex-M3 Mutex validation fixture" OFF)
if(INSIGHT_TEST_MUTEX)
    if(NOT INSIGHT_DISPLAY_RTOS OR NOT INSIGHT_RTOS_ENABLED OR NOT INSIGHT_DIAG_DL OR
       NOT INSIGHT_DIAG_EXPLAIN OR INSIGHT_APP_INTEGRATED_DIAGNOSTICS OR
       INSIGHT_TEST_MSP OR INSIGHT_TEST_TIMING OR INSIGHT_TEST_JOB OR INSIGHT_TEST_STRESS)
        message(FATAL_ERROR "Mutex bench requires standalone display-target startup, Trace/Explain and DL")
    endif()
    get_target_property(mutex_sources ${CMAKE_PROJECT_NAME} SOURCES)
    list(FILTER mutex_sources EXCLUDE REGEX "/app_tasks[.]c$")
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SOURCES "${mutex_sources}")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE tests/hardware/mutex_fixture.c)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE tests/hardware)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE INSIGHT_TEST_MUTEX=1)
endif()
