#include "ui_screenshot.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "driver/uart_vfs.h"
#include "driver/uart.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdatomic.h>

static const char *TAG = "screenshot";
static atomic_bool s_busy;

/* Eight hexadecimal digits per run: 16-bit count, then RGB565 color.
   Text framing avoids console newline conversion, and RLE greatly reduces
   the otherwise 90-second transfer of mostly flat UI surfaces. */
static void append_run(char *encoded, size_t *used, uint16_t count, uint16_t color) {
    static const char hex[] = "0123456789abcdef";
    uint32_t record = ((uint32_t)count << 16) | color;
    for (int shift = 28; shift >= 0; shift -= 4)
        encoded[(*used)++] = hex[(record >> shift) & 15];
}

static void screenshot_worker(void *arg) {
    (void)arg;
    const uart_port_t port = CONFIG_ESP_CONSOLE_UART_NUM;
    if (!uart_is_driver_installed(port) &&
        uart_driver_install(port, 1024, 4096, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize binary UART capture");
        atomic_store(&s_busy, false); vTaskDelete(NULL);
    }
    // Buffered console I/O also lets CPU0 sleep during the long transfer.
    uart_vfs_dev_use_driver(port);
    /* Hold the LVGL lock only for the snapshot, not for serial output. */
    bsp_display_lock(0);
    lv_draw_buf_t *shot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    bsp_display_unlock();

    if (!shot) {
        ESP_LOGE(TAG, "lv_snapshot_take failed (OOM?)");
        atomic_store(&s_busy, false);
        vTaskDelete(NULL);
    }

    uint32_t w = shot->header.w;
    uint32_t h = shot->header.h;
    uint32_t stride = shot->header.stride;  /* bytes per row, may include padding */

    /* Silence all log output during binary transfer so no log lines can
       corrupt the pixel stream. Restore to INFO when done. */
    esp_log_level_set("*", ESP_LOG_NONE);
    fflush(stdout);
    uart_vfs_dev_port_set_tx_line_endings(port, ESP_LINE_ENDINGS_LF);
    printf("SCRN\n%" PRIu32 "\n%" PRIu32 "\nRLE565\n", w, h);
    fflush(stdout);
    uart_wait_tx_done(port, pdMS_TO_TICKS(2000));

    const uint8_t *row = shot->data;
    uint32_t row_bytes = w * 2;
    uint32_t checksum = 0;
    char encoded[513];
    size_t used = 0;
    uint16_t run_color = 0, run_count = 0;
    bool transfer_ok = true;
    for (uint32_t y = 0; y < h; y++) {
        checksum = esp_rom_crc32_le(checksum, row, row_bytes);
        for (uint32_t x = 0; x < row_bytes; x += 2) {
            uint16_t color = row[x] | ((uint16_t)row[x + 1] << 8);
            if (run_count && (color != run_color || run_count == UINT16_MAX)) {
                append_run(encoded, &used, run_count, run_color);
                if (used == 512) {
                    encoded[used++] = '\n';
                    if (uart_write_bytes(port, encoded, used) != used) transfer_ok = false;
                    used = 0;
                }
                run_count = 0;
            }
            run_color = color;
            run_count++;
        }
        row += stride;
        if ((y % 16) == 15) vTaskDelay(1);
    }
    if (run_count) append_run(encoded, &used, run_count, run_color);
    if (used) {
        encoded[used++] = '\n';
        if (uart_write_bytes(port, encoded, used) != used) transfer_ok = false;
    }
    uart_wait_tx_done(port, pdMS_TO_TICKS(2000));

    printf("\nSCRN_END\nSCRN_CRC32 %08" PRIx32 "\n", checksum);
    fflush(stdout);
    uart_wait_tx_done(port, pdMS_TO_TICKS(2000));
    esp_log_level_set("*", ESP_LOG_INFO);  /* restore logging */

    lv_draw_buf_destroy(shot);
    if (!transfer_ok) ESP_LOGE(TAG, "Screenshot UART write failed; discard capture");
    ESP_LOGI(TAG, "Screenshot sent: %" PRIu32 "x%" PRIu32 " (%" PRIu32 " bytes raw)",
             w, h, w * h * 2);
    atomic_store(&s_busy, false);
    vTaskDelete(NULL);
}

void ui_screenshot_take(void) {
    if (atomic_exchange(&s_busy, true)) return;
    // Full-screen snapshot rendering exceeds the console's 4 KB stack.
    // Allocate a sufficiently sized internal stack only during a capture.
    if (xTaskCreate(screenshot_worker, "screen_capture", 20480, NULL, 3, NULL) != pdPASS) {
        atomic_store(&s_busy, false);
        ESP_LOGE(TAG, "Cannot allocate screenshot worker");
    }
}
