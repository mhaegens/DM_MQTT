#pragma once
#include <stdbool.h>
#include "cJSON.h"
// Validation is shared by boot loading and portal/profile uploads.
bool config_root_valid(const cJSON *root);
