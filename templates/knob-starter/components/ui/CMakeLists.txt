idf_component_register(
    SRCS "ui.c" "knob.c" "knob_decode.c" "backlight.c" "ui_panel_init_cmds.c" "fonts/font_muli_28.c"
    INCLUDE_DIRS "include" "fonts"
    # Each driver is named: IDF 6 no longer hands out LEDC, GPIO and SPI through the `driver` umbrella.
    REQUIRES esp_driver_ledc esp_driver_gpio esp_driver_spi esp_driver_i2c esp_lcd esp_timer board
)

# Generated LVGL fonts guard their include with LV_LVGL_H_INCLUDE_SIMPLE → use "lvgl.h".
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
