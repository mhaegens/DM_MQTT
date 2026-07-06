idf_component_register(
    SRCS
        "main.c"
        "config_loader.c"
        "default_config.c"
        "wifi_manager.c"
        "wifi_portal.c"
        "mqtt_task.c"
        "sim_task.c"
        "ui/ui_main.c"
        "ui/ui_home.c"
        "ui/ui_machines.c"
        "ui/ui_tanks.c"
        "ui/ui_debug.c"
        "ui/ui_nfc.c"
        "ui/ui_detail.c"
        "ui/ui_tank_overview.c"
        "ui/ui_screenshot.c"
        "ui/ui_wifi_setup.c"
        "ui/ui_wifi_password.c"
        "ui/ui_mqtt_detail.c"
        "ui/data.c"
        "ui/assets/sap_logo.c"
        "ui/assets/tank_icon.c"
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
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        fatfs
        sdmmc
        json
        spiffs
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)

# -O2 (CONFIG_COMPILER_OPTIMIZATION_PERF) enables deeper stringop/format analysis
# that flags strncpy(dst, src, sizeof(dst)-1) and bounded snprintf payloads as
# potentially truncating. The code explicitly null-terminates or intentionally
# bounds these writes; downgrade both from errors to plain warnings so the
# perf build stays green without rewriting the entire string-handling layer.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=stringop-truncation
    -Wno-error=format-truncation
)

# Package spiffs_image/ directory into a SPIFFS image for the 'spiffs' partition.
# FLASH_IN_PROJECT means 'idf.py flash' flashes both firmware and SPIFFS image.
spiffs_create_partition_image(spiffs spiffs_image FLASH_IN_PROJECT)
