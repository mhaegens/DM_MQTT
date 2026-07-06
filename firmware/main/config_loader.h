#pragma once
#include <stdbool.h>

typedef enum {
    CONFIG_OK,
    CONFIG_SPIFFS_OK,
    CONFIG_SD_NOT_FOUND,
    CONFIG_FILE_NOT_FOUND,
    CONFIG_PARSE_ERROR,
    CONFIG_USING_DEFAULT,
} ConfigResult;

ConfigResult    config_load(void);
const char     *config_result_str(ConfigResult r);

// Sanity-check a JSON config string (parseable + has at least one of the
// machines/tanks/people arrays) without touching the entity arrays. Used by
// the captive portal to reject broken uploads before they are persisted.
bool config_json_valid(const char *json_str);
