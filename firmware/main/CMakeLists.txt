idf_component_register(
    SRCS
        "main.c"
        "data_model.c"
        "mqtt_task.c"
        "sim_task.c"
        "wifi_simple.c"
        "hw/buttons.c"
        "ui/ui_main.c"
        "ui/ui_home.c"
        "ui/ui_machines.c"
        "ui/ui_tanks.c"
        "ui/ui_debug.c"
        "ui/ui_nfc.c"
        "ui/ui_detail.c"
    INCLUDE_DIRS
        "."
        "ui"
        "hw"
        "${CMAKE_SOURCE_DIR}/fonts"
    REQUIRES
        nvs_flash
        esp_event
        esp_netif
        esp_wifi
        mqtt
        esp_timer
        driver
        freertos
        log
        # BSP provides display + touch + LVGL; add when BSP component is present:
        # waveshare_bsp
        # lvgl
)
