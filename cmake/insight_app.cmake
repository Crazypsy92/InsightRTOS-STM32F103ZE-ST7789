# cmake/insight_app.cmake
# Source manifest and configuration for INSIGHT_DISPLAY_RTOS

get_target_property(display_sources ${CMAKE_PROJECT_NAME} SOURCES)
list(FILTER display_sources EXCLUDE REGEX "/(rtos_smoke_app|app_lcd_test)[.]c$")
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SOURCES "${display_sources}")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Src/app_tasks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Src/app_ipc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Src/app_events.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/display_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/display_pages.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/display_policy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/display_ui.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE INSIGHT_DISPLAY_RTOS=1)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Core/display_pages.c PROPERTIES COMPILE_OPTIONS "-fno-inline")
