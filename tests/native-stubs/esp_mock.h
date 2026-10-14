#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#define ESP_OK 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGD(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define SDMMC_HOST_SLOT_0 0
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define GPIO_MODE_OUTPUT 1
#define SDMMC_HOST_DEFAULT() {0}
#define SDMMC_SLOT_CONFIG_DEFAULT() {0}
typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef void *SemaphoreHandle_t;
typedef void *esp_ldo_channel_handle_t;
typedef struct { int slot; } sdmmc_host_t;
typedef struct { int width,clk,cmd,d0,d1,d2,d3,flags; } sdmmc_slot_config_t;
typedef struct { int unused; } sdmmc_card_t;
typedef struct { int chan_id,voltage_mv; } esp_ldo_channel_config_t;
typedef struct { const char *base_path,*partition_label; int max_files; bool format_if_mount_failed; } esp_vfs_spiffs_conf_t;
typedef struct { bool format_if_mount_failed; int max_files,allocation_unit_size; } esp_vfs_fat_sdmmc_mount_config_t;
static inline void *heap_caps_malloc(size_t n,int caps) { (void)caps;return malloc(n); }
static inline void *heap_caps_realloc(void *p,size_t n,int caps) { (void)caps;return realloc(p,n); }
static inline void *xSemaphoreCreateRecursiveMutex(void) { return (void *)1; }
static inline int xSemaphoreTakeRecursive(void *p,int ticks) { (void)p;(void)ticks;return 1; }
static inline int xSemaphoreGiveRecursive(void *p) { (void)p;return 1; }
static inline void vTaskDelay(int ticks) { (void)ticks; }
static inline bool esp_spiffs_mounted(const char *p) { (void)p;return true; }
static inline int esp_vfs_spiffs_register(const void *c) { (void)c;return 0; }
static inline int esp_ldo_acquire_channel(const void *c,void **h) { (void)c;*h=(void *)1;return 0; }
static inline void gpio_set_direction(int pin,int mode) { (void)pin;(void)mode; }
static inline void gpio_set_level(int pin,int level) { (void)pin;(void)level; }
static inline int esp_vfs_fat_sdmmc_mount(const char *p,const void *h,const void *s,const void *c,sdmmc_card_t **out) { (void)p;(void)h;(void)s;(void)c;static sdmmc_card_t card;*out=&card;return 0; }
static inline int esp_vfs_fat_info(const char *path,uint64_t *total,uint64_t *available) { struct statvfs s; if(statvfs(path,&s))return -1;*total=(uint64_t)s.f_blocks*s.f_frsize;*available=(uint64_t)s.f_bavail*s.f_frsize;return 0; }
