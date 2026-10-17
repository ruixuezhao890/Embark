SET(SOURCES
    components/ui_comp.c
    components/ui_comp_hook.c
    ui.c
    ui_helpers.c
    ui_comm.c
    screens/ui_launcher.c
    screens/ui_clock.c
    screens/ui_weather.c
    screens/ui_music.c
    screens/ui_album.c
    screens/ui_settings.c
    screens/ui_about.c
)

add_library(ui ${SOURCES})
target_include_directories(ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(TARGET lvgl::lvgl)
    target_link_libraries(ui PUBLIC lvgl::lvgl)
elseif(TARGET lvgl)
    target_link_libraries(ui PUBLIC lvgl)
endif()
