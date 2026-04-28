#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  audio.h — ES8311 I2S audio stub (Phase 9).
//
//  GPIO9 (I2S DSDIN) is shared with the BOOT button.
//  Before calling audio_init(), call buttons_deinit() to release GPIO9.
//  This is gated by the AUDIO_ENABLED compile-time flag; set it in
//  sdkconfig or CMakeLists to enable audio in Phase 9.
// ─────────────────────────────────────────────────────────────────────────────

#ifdef CONFIG_SAPDM_AUDIO_ENABLED
void audio_init(void);
void audio_play_click(void);    // 220 Hz, 40 ms — NFC confirm, PULSE tap
void audio_play_chime(void);    // two-tone up — MQTT connected
void audio_play_low_tone(void); // low tone — MQTT disconnected, machine stopped
#else
static inline void audio_init(void)        {}
static inline void audio_play_click(void)  {}
static inline void audio_play_chime(void)  {}
static inline void audio_play_low_tone(void) {}
#endif
