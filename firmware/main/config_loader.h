#pragma once

typedef enum {
    CONFIG_OK,
    CONFIG_SD_NOT_FOUND,
    CONFIG_FILE_NOT_FOUND,
    CONFIG_PARSE_ERROR,
    CONFIG_USING_DEFAULT,
} ConfigResult;

ConfigResult    config_load(void);
const char     *config_result_str(ConfigResult r);
