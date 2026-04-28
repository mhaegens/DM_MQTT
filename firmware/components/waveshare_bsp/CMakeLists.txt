cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "waveshare_p4.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
        esp_lvgl_port
        esp_ldo_regulator
        driver
        freertos
        log
        esp_lcd_st7703
)
