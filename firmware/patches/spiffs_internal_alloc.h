#pragma once
#include <stdlib.h>
#include "esp_heap_caps.h"

// SPIFFS repeatedly reads flash metadata into its small work/cache buffers.
// Keep those in internal RAM: placing them in PSRAM makes each flash read
// allocate a bounce buffer and scan the fragmented heap under heavy UI load.
// This header is applied only to the ESP-IDF SPIFFS component, leaving normal
// application/UI allocations in PSRAM and preserving the filesystem format.
#define calloc(count, size) heap_caps_calloc((count), (size), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
