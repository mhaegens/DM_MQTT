#include "bsp/waveshare_p4.h"
#include "config.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_st7703.h"
#include "esp_ldo_regulator.h"
#include "esp_lvgl_port.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"

// ─────────────────────────────────────────────────────────────────────────────
//  waveshare_p4.c — BSP for Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
//
//  Panel IC:  ST7703 (MIPI-DSI 2-lane, 720×720)
//  Touch:     GT911 (I2C, SDA=GPIO7, SCL=GPIO8, addr 0x5D)
//  Backlight: GPIO26 (active LOW per Waveshare demo)
//  Reset:     GPIO27
//  LDO ch3:   2500 mV for MIPI DSI PHY power rail
//
//  Init pattern mirrors ESP-IDF/07_color_panel from the Waveshare demo zip.
// ─────────────────────────────────────────────────────────────────────────────

static const char *TAG = "bsp";

static SemaphoreHandle_t       s_lvgl_mutex = NULL;
static esp_lcd_panel_handle_t  s_panel      = NULL;
static esp_lcd_touch_handle_t  s_touch      = NULL;
static esp_ldo_channel_handle_t s_ldo_mipi_phy = NULL;

// ── GT911 touch init ──────────────────────────────────────────────────────────
static esp_err_t init_touch(void)
{
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_0,
        .scl_io_num                   = GPIO_I2C_SCL,
        .sda_io_num                   = GPIO_I2C_SDA,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = false,
    };
    i2c_master_bus_handle_t i2c_bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &i2c_bus), TAG, "I2C bus init failed");

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr              = 0x5D,   // GT911 default; try 0x14 if not detected
        .control_phase_bytes   = 1,
        .dc_bit_offset         = 0,
        .lcd_cmd_bits          = 16,
        .lcd_param_bits        = 8,
        .flags.disable_control_phase = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io),
                        TAG, "Touch panel IO init failed");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BSP_LCD_H_RES,
        .y_max        = BSP_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    return esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
}

// ── LVGL input device callback ────────────────────────────────────────────────
static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;
    uint8_t count;
    esp_lcd_touch_read_data(s_touch);
    if (esp_lcd_touch_get_coordinates(s_touch, &x, &y, NULL, &count, 1) && count > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── DPI panel flush callback ──────────────────────────────────────────────────
// ST7703 is a DPI (video mode) panel — the driver handles frame sync internally.
// LVGL just needs to know when the draw buffer can be reused.
static bool dpi_flush_ready_cb(esp_lcd_panel_handle_t panel,
                               esp_lcd_dpi_panel_event_data_t *edata,
                               void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map)
{
    esp_lcd_panel_draw_bitmap(s_panel,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    // flush_ready is called from the DPI vsync callback above
}

// ── Public API ────────────────────────────────────────────────────────────────
esp_err_t bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    ESP_LOGI(TAG, "BSP init — Waveshare ESP32-P4 720x720 MIPI-DSI (ST7703)");

    // ── Backlight (GPIO26, active LOW) ────────────────────────────────────────
    gpio_config_t bl_cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BSP_LCD_BL_GPIO,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg), TAG, "Backlight GPIO config failed");
    gpio_set_level(BSP_LCD_BL_GPIO, BSP_LCD_BL_ON);

    // ── LDO power rail for MIPI DSI PHY ──────────────────────────────────────
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_ldo_mipi_phy),
                        TAG, "LDO acquire failed");
    ESP_LOGI(TAG, "MIPI DSI PHY powered on (LDO ch%d, %dmV)",
             BSP_MIPI_DSI_PHY_PWR_LDO_CHAN, BSP_MIPI_DSI_PHY_PWR_LDO_MV);

    // ── MIPI-DSI bus (2-lane, config from ST7703 component macro) ────────────
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t dsi_bus_cfg = ST7703_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi_bus_cfg, &dsi_bus),
                        TAG, "DSI bus init failed");

    // ── Panel IO (DBI over DSI) ───────────────────────────────────────────────
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_dbi_io_config_t dbi_cfg = ST7703_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &panel_io),
                        TAG, "Panel IO init failed");

    // ── Panel device (ST7703, 720×720, 60Hz, RGB666) ─────────────────────────
    esp_lcd_dpi_panel_config_t dpi_cfg =
        ST7703_720_720_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB666);

    st7703_vendor_config_t vendor_cfg = {
        .flags = { .use_mipi_interface = 1 },
        .mipi_config = {
            .dsi_bus    = dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_LCD_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 18,   // RGB666
        .vendor_config  = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7703(panel_io, &panel_cfg, &s_panel),
                        TAG, "ST7703 panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel),  TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),   TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Display on failed");

    // ── Touch ─────────────────────────────────────────────────────────────────
    ESP_RETURN_ON_ERROR(init_touch(), TAG, "Touch init failed");

    // ── LVGL port ─────────────────────────────────────────────────────────────
    s_lvgl_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&cfg->lvgl_port_cfg), TAG, "LVGL port init failed");

    // Double-buffered display, 80-line buffers in PSRAM
    size_t buf_size = BSP_LCD_H_RES * 80 * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);

    lv_display_t *disp = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    // Wire DPI vsync → flush_ready so LVGL knows when to reuse the buffer
    esp_lcd_dpi_panel_event_callbacks_t dpi_cbs = {
        .on_color_trans_done = dpi_flush_ready_cb,
    };
    esp_lcd_dpi_panel_register_event_callbacks(s_panel, &dpi_cbs, disp);

    // Touch input device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);

    ESP_LOGI(TAG, "BSP ready — %dx%d ST7703, touch GT911 on I2C GPIO%d/GPIO%d",
             BSP_LCD_H_RES, BSP_LCD_V_RES, GPIO_I2C_SDA, GPIO_I2C_SCL);
    return ESP_OK;
}

void bsp_display_lock(uint32_t timeout_ms)
{
    if (s_lvgl_mutex)
        xSemaphoreTake(s_lvgl_mutex,
                       timeout_ms == 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
}

void bsp_display_unlock(void)
{
    if (s_lvgl_mutex) xSemaphoreGive(s_lvgl_mutex);
}
