#include "ui_screenshot.h"
#include "lvgl.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "screenshot";

void ui_screenshot_take(void) {
    /* Hold the LVGL lock only for the snapshot, not for serial output. */
    bsp_display_lock(0);
    lv_draw_buf_t *shot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    bsp_display_unlock();

    if (!shot) {
        ESP_LOGE(TAG, "lv_snapshot_take failed (OOM?)");
        return;
    }

    uint32_t w = shot->header.w;
    uint32_t h = shot->header.h;
    uint32_t stride = shot->header.stride;  /* bytes per row, may include padding */

    printf("SCRN\n%" PRIu32 "\n%" PRIu32 "\nRGB565\n", w, h);

    /* Silence all log output during binary transfer so no log lines can
       corrupt the pixel stream. Restore to INFO when done. */
    esp_log_level_set("*", ESP_LOG_NONE);

    const uint8_t *row = shot->data;
    uint32_t row_bytes = w * 2;
    for (uint32_t y = 0; y < h; y++) {
        fwrite(row, 1, row_bytes, stdout);
        row += stride;
        /* Yield every 10 rows so the IDLE task runs and the watchdog stays fed.
           10 rows × ~125 ms/row = ~1.25 s between yields, well under the 5 s WDT. */
        if ((y & 9) == 9) vTaskDelay(1);
    }
    fflush(stdout);

    esp_log_level_set("*", ESP_LOG_INFO);  /* restore logging */

    printf("\nSCRN_END\n");
    fflush(stdout);

    lv_draw_buf_destroy(shot);
    ESP_LOGI(TAG, "Screenshot sent: %" PRIu32 "x%" PRIu32 " (%" PRIu32 " bytes raw)",
             w, h, w * h * 2);
}
