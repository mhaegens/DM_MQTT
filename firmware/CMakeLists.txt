cmake_minimum_required(VERSION 3.16)

# Include Waveshare BSP as a component (download from Waveshare wiki and place in
# firmware/components/waveshare_bsp/).
# The BSP provides display init (MIPI-DSI), GT911 touch, and the LVGL port for P4.
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sapdm_simulator)

# ── Auto-apply the esp_lvgl_port avoid-tear direct-mode patch ─────────────────
# The Waveshare BSP's avoid-tear path hands esp_lvgl_port two full-frame PSRAM
# buffers but sets neither full_refresh nor direct_mode, so the port falls into
# PARTIAL render mode and the panel alternates between two half-stale buffers.
# patches/esp_lvgl_port_avoid_tearing_direct_mode.patch fixes that; it used to
# be applied by hand after every `idf.py update-dependencies`, and one missed
# re-apply silently broke the display pipeline. Apply it here, idempotently.
set(_lvgl_port_dir  "${CMAKE_CURRENT_SOURCE_DIR}/managed_components/espressif__esp_lvgl_port")
set(_lvgl_port_disp "${_lvgl_port_dir}/src/lvgl9/esp_lvgl_port_disp.c")
if(EXISTS "${_lvgl_port_disp}")
    file(READ "${_lvgl_port_disp}" _disp_src)
    if(_disp_src MATCHES "avoid_tearing uses panel-owned")
        message(STATUS "esp_lvgl_port: direct-mode avoid-tear patch already applied")
    else()
        set(_anchor "    } else if (disp_cfg->flags.direct_mode) {")
        set(_insert "    } else if (priv_cfg && priv_cfg->avoid_tearing) {
        /* avoid_tearing uses panel-owned full-frame PSRAM buffers. Direct mode:
         * LVGL renders only dirty regions into the active framebuffer and the
         * port syncs dirty areas between the two buffers on flush - far cheaper
         * than RENDER_MODE_FULL, which repaints all 720x720 on any change. */
        disp_ctx->flags.direct_mode = 1;
        lv_display_set_buffers(disp, buf1, buf2, buffer_size * color_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);

    } else if (disp_cfg->flags.direct_mode) {")
        string(FIND "${_disp_src}" "${_anchor}" _anchor_pos)
        if(_anchor_pos EQUAL -1)
            message(FATAL_ERROR
                "esp_lvgl_port: patch anchor not found in ${_lvgl_port_disp}. "
                "Upstream esp_lvgl_port changed; re-derive "
                "patches/esp_lvgl_port_avoid_tearing_direct_mode.patch before "
                "building, or avoid-tear runs in PARTIAL mode with two stale "
                "framebuffers (visual corruption).")
        endif()
        string(REPLACE "${_anchor}" "${_insert}" _disp_src "${_disp_src}")
        file(WRITE "${_lvgl_port_disp}" "${_disp_src}")
        # The component manager refuses to build when a managed component's
        # content no longer matches its hash file; removing the hash marks the
        # component as developer-managed, which is exactly what a patch is.
        file(REMOVE "${_lvgl_port_dir}/.component_hash")
        message(STATUS "esp_lvgl_port: applied direct-mode avoid-tear patch")
    endif()
else()
    message(STATUS "esp_lvgl_port not downloaded yet — the direct-mode patch "
                   "is applied automatically on the next reconfigure")
endif()
