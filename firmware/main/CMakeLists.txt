idf_component_register(
    SRCS
        "main.c"
        "ui/ui_main.c"
        "ui/ui_home.c"
        "ui/ui_machines.c"
        "ui/ui_tanks.c"
        "ui/ui_debug.c"
        "ui/ui_nfc.c"
        "ui/ui_detail.c"
        "ui/ui_tank_overview.c"
        "ui/data.c"
        "ui/assets/sap_logo.c"
    INCLUDE_DIRS
        "."
        "ui"
        "ui/assets"
    REQUIRES
        waveshare__esp32_p4_wifi6_touch_lcd_4b
        espressif__esp_lvgl_port
        log
        freertos
        esp_timer
        mqtt
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)
