#pragma once
#include <stdbool.h>

// ─────────────────────────────────────────────────────────────────────────────
//  buttons.h — GPIO9 BOOT button handler.
//
//  GPIO9 is shared with I2S DSDIN (audio data out to ES8311).
//  Audio and button use are mutually exclusive: if audio is enabled (Phase 9),
//  the button handler must be disabled before I2S is initialised.
//  Controlled by the BUTTONS_AUDIO_CONFLICT compile-time flag in audio.h.
//
//  Short press (< 400 ms): decrement active detail-view metric by one step
//                           OR trigger PULSE on last-selected machine (if not in detail view)
//  Long press  (≥ 400 ms): increment active detail-view metric by one step
//
//  TODO: If the WIFI6-Touch-LCD-4B wiki reveals a second USER button GPIO,
//        assign it to the opposite direction for clean up/down split.
// ─────────────────────────────────────────────────────────────────────────────

typedef enum {
    BTN_EVENT_SHORT_PRESS,
    BTN_EVENT_LONG_PRESS,
} BtnEvent;

typedef void (*btn_callback_t)(BtnEvent event);

void buttons_init(btn_callback_t cb);
void buttons_deinit(void);   // call before audio_init() if enabling audio
