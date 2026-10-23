SET(SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ui_comp.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ui_comp_hook.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui_helpers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui_comm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts/ui_fonts.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/animations/ui_animations.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/screens/ui_launcher.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/events/aido_event_launcher.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/screens/ui_clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/events/aido_event_clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/screens/ui_weather.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/events/aido_event_weather.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/screens/ui_music.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/events/aido_event_music.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/screens/ui_album.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/events/aido_event_album.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/screens/ui_settings.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/events/aido_event_settings.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/custom/custom.c"
)

file(GLOB_RECURSE AIDOGUI_USER_SOURCES CONFIGURE_DEPENDS "custom/*.c")
foreach(USER_SOURCE IN LISTS AIDOGUI_USER_SOURCES)
    string(TOLOWER "${USER_SOURCE}" LOWER_USER_SOURCE)
    # 旧扩展注册文件保留在磁盘供用户迁移，但不能链接进新代码。
    if(NOT LOWER_USER_SOURCE MATCHES "/custom/(user/(aidogui_user_hooks\\.c|simulator/aidogui_user_simulator\\.c)|project/(aidogui_project_hooks\\.c|simulator/aidogui_project_simulator\\.c))$")
        list(APPEND SOURCES "${USER_SOURCE}")
    endif()
endforeach()
list(REMOVE_DUPLICATES SOURCES)
add_library(ui ${SOURCES})
target_include_directories(ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/custom")
if(TARGET lvgl::lvgl)
    target_link_libraries(ui PUBLIC lvgl::lvgl)
elseif(TARGET lvgl)
    target_link_libraries(ui PUBLIC lvgl)
endif()
