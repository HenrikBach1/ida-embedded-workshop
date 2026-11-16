idf_component_register(
    SRCS "app.c" "app_report.c" "app_monitor.c" "app_nav.c" "app_widgets.c"
         "screen_home.c" "screen_events.c" "screen_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES ui theme haptic net mic sound settings esp_timer
)
