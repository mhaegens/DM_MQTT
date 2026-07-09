#pragma once
#include <stdbool.h>

// ─────────────────────────────────────────────────────────────────────────────
//  config_store.h — Config template storage shared by the captive portal and
//  the on-device Configuration screen.
//
//  The SD card is the home of the profile library: bundled flash templates
//  are seeded onto it at boot (config_store_seed_sd) and portal uploads are
//  stored on it as named templates (config_store_save_template), so profiles
//  survive switching and reboots. Flash (SPIFFS) is the fallback when no
//  card is inserted.
//
//  The ACTIVE config is sapdm_config.json, kept in sync on BOTH stores
//  (SPIFFS + SD card) by every write; at boot an inserted SD card outranks
//  SPIFFS (see config_loader.c). Any other *.json on either store is a
//  selectable TEMPLATE; activating one copies its content over the active
//  file in both stores.
// ─────────────────────────────────────────────────────────────────────────────

#define CFG_TPL_MAX           12
#define CFG_TPL_NAME_MAX      48
#define CFG_TPL_PLANT_MAX     40
// Template name used when an upload carries no usable filename
#define CFG_TPL_FALLBACK_NAME "uploaded_config.json"

typedef struct {
    char name[CFG_TPL_NAME_MAX];    // filename, e.g. "config_bakery.json"
    char plant[CFG_TPL_PLANT_MAX];  // plant_name from the JSON ("" if absent)
    int  machines;                  // entity counts for the list subtitle
    int  tanks;
    bool from_sd;                   // true: /sdcard, false: /spiffs
    bool active;                    // content-identical to the active config
} ConfigTemplate;

// Create the store lock — call once at boot before any other function here.
void config_store_init(void);

// List template files on SPIFFS + SD (active file itself excluded).
// Returns the number of entries written to out (≤ max).
int config_store_list(ConfigTemplate *out, int max);

// Copy the named template over the active config in both stores.
// Validates the file first. Returns NULL on success, else a static/const
// error string suitable for showing to the user.
const char *config_store_activate(const char *name, bool from_sd);

// Persist a validated config blob as the active config in both stores.
// Returns NULL on success, else an error string.
const char *config_store_save_active(const char *data, int len);

// Persist a validated config blob as a named template — SD card preferred,
// flash fallback. Same-named files are overwritten. saved_to_sd (optional)
// reports where it landed. Returns NULL on success, else an error string.
const char *config_store_save_template(const char *name, const char *data,
                                       int len, bool *saved_to_sd);

// Reduce a client-supplied filename to a safe template name: basename only,
// hostile characters replaced, ".json" enforced, reserved names redirected
// to CFG_TPL_FALLBACK_NAME. out should hold CFG_TPL_NAME_MAX bytes.
void config_store_sanitize_tpl_name(const char *raw, char *out, int cap);

// Copy bundled flash *.json files missing from the SD card onto it, so the
// card carries the full profile library. Card copies are never overwritten.
// Call once at boot. Returns files copied, or -1 when no card is present.
int config_store_seed_sd(void);
