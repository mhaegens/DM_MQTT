#include "ui_tank_shape.h"
#include <math.h>

static int32_t inset(int32_t w) { return w / 14; }
static int32_t cap_radius(int32_t w) { int32_t r = (w - 2 * inset(w)) / 5; return r > 36 ? 36 : r; }
int32_t ui_tank_shape_join_y(int32_t h) { return h * 64 / 100; }
int32_t ui_tank_shape_level_y(int32_t h, float level)
{
    if (!isfinite(level) || level < 0) level = 0;
    if (level > 100) level = 100;
    return 12 + (int32_t)((h - 50) * (100 - level) / 100.0f);
}

int32_t ui_tank_shape_right_at(int32_t w, int32_t h, float level)
{
    int32_t y = ui_tank_shape_level_y(h, level);
    int32_t join = ui_tank_shape_join_y(h), bottom = h - 38;
    int32_t half = w / 2 - inset(w);
    if (y > join) half = 9 + (half - 9) * (bottom - y) / (bottom - join);
    else if (y < 12 + cap_radius(w)) {
        int32_t radius = cap_radius(w), dy = 12 + radius - y;
        half -= radius - (int32_t)sqrtf((float)(radius * radius - dy * dy));
    }
    return w / 2 + half;
}

static void rect(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                 lv_color_t color, lv_opa_t opacity, int32_t radius)
{
    if (x2 < x1 || y2 < y1) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color; d.bg_opa = opacity; d.radius = radius;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &d, &a);
}

static void line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                 lv_color_t color, lv_opa_t opacity, int32_t width)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
    d.color = color; d.opa = opacity; d.width = width;
    lv_draw_line(layer, &d);
}

static void cone(lv_layer_t *layer, int32_t left, int32_t right, int32_t join,
                 int32_t center, int32_t bottom, lv_color_t color, lv_opa_t opacity)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = color; d.opa = opacity;
    d.p[0].x = left; d.p[0].y = join;
    d.p[1].x = right; d.p[1].y = join;
    // One clipped triangle avoids an anti-aliased diagonal seam where two
    // triangles meet. Extend its apex so the clipped base remains 18px wide.
    int32_t half_width = (right - left) / 2;
    d.p[2].x = center;
    d.p[2].y = join + (bottom - join) * half_width / (half_width - 9);
    lv_area_t clip = layer->_clip_area;
    if (layer->_clip_area.y2 > bottom) layer->_clip_area.y2 = bottom;
    lv_draw_triangle(layer, &d);
    layer->_clip_area = clip;
}

static void vessel(lv_layer_t *layer, int32_t left, int32_t right, int32_t top,
                   int32_t join, int32_t center, int32_t bottom, int32_t radius,
                   lv_color_t color)
{
    rect(layer, left, top, right, join, color, LV_OPA_COVER, radius);
    // Square the body/cone junction while preserving rounded shoulders.
    rect(layer, left, top + radius, right, join, color, LV_OPA_COVER, 0);
    cone(layer, left, right, join, center, bottom, color, LV_OPA_COVER);
}

void ui_tank_shape_draw(lv_layer_t *layer, const lv_area_t *area, float level, lv_color_t fill)
{
    int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    if (w < 60 || h < 100) return;
    if (!isfinite(level) || level < 0) level = 0;
    if (level > 100) level = 100;
    int32_t left = area->x1 + inset(w), right = area->x2 - inset(w);
    int32_t center = area->x1 + w / 2, top = area->y1 + 12;
    int32_t join = area->y1 + ui_tank_shape_join_y(h), bottom = area->y1 + h - 38;
    int32_t radius = cap_radius(w), waterline = area->y1 + ui_tank_shape_level_y(h, level);
    lv_color_t steel = lv_color_hex(0x74869B), outline = lv_color_hex(0x7D8DA2);
    lv_color_t body = lv_color_hex(0xEDF2F7);

    // Slim support legs sit behind the vessel, not in the liquid.
    int32_t legs[] = {left + (right - left) / 5, right - (right - left) / 5};
    for (int i = 0; i < 2; ++i) {
        rect(layer, legs[i] - 3, join + 12, legs[i] + 3, area->y2 - 4, steel, LV_OPA_COVER, 2);
        rect(layer, legs[i] - 10, area->y2 - 5, legs[i] + 10, area->y2 - 2, steel, LV_OPA_COVER, 2);
    }
    vessel(layer, left, right, top, join, center, bottom, radius, body);

    if (level > 0) {
        // LVGL copies this clip into each new draw task. Reusing the exact
        // vessel geometry keeps liquid/highlights inside its shoulders/cone.
        lv_area_t saved_clip = layer->_clip_area;
        lv_area_t fill_clip = {left, waterline, right, bottom};
        layer->_clip_area.x1 = saved_clip.x1 > fill_clip.x1 ? saved_clip.x1 : fill_clip.x1;
        layer->_clip_area.x2 = saved_clip.x2 < fill_clip.x2 ? saved_clip.x2 : fill_clip.x2;
        layer->_clip_area.y1 = saved_clip.y1 > fill_clip.y1 ? saved_clip.y1 : fill_clip.y1;
        layer->_clip_area.y2 = saved_clip.y2 < fill_clip.y2 ? saved_clip.y2 : fill_clip.y2;
        if (layer->_clip_area.x1 <= layer->_clip_area.x2 && layer->_clip_area.y1 <= layer->_clip_area.y2) {
            vessel(layer, left, right, top, join, center, bottom, radius, fill);
            rect(layer, left + 9, top + radius + 5, left + 14, join - 8,
                 lv_color_white(), LV_OPA_20, 2);
        }
        layer->_clip_area = saved_clip;
        int32_t surface_right = area->x1 + ui_tank_shape_right_at(w, h, level);
        int32_t surface_left = 2 * center - surface_right;
        if (surface_right - surface_left > 12)
            line(layer, surface_left + 3, waterline + 1, surface_right - 3, waterline + 1,
                 lv_color_white(), LV_OPA_40, 2);
    }

    // Outline after the fill: rounded shoulders, straight cylinder and cone.
    line(layer, left, top + radius, left, join, outline, LV_OPA_COVER, 2);
    line(layer, right, top + radius, right, join, outline, LV_OPA_COVER, 2);
    line(layer, left + radius, top, right - radius, top, outline, LV_OPA_COVER, 2);
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = outline; arc.width = 2; arc.radius = radius;
    arc.center.x = left + radius; arc.center.y = top + radius;
    arc.start_angle = 180; arc.end_angle = 270;
    lv_draw_arc(layer, &arc);
    arc.center.x = right - radius; arc.start_angle = 270; arc.end_angle = 360;
    lv_draw_arc(layer, &arc);
    line(layer, left, join, center - 9, bottom, outline, LV_OPA_COVER, 2);
    line(layer, right, join, center + 9, bottom, outline, LV_OPA_COVER, 2);
    line(layer, left + 1, join, right - 1, join, steel, LV_OPA_40, 1);
    // Access lid and a short discharge outlet give the cone a clear purpose.
    rect(layer, center - 13, top - 6, center + 13, top - 1, steel, LV_OPA_COVER, 3);
    rect(layer, center - 8, bottom, center + 8, bottom + 15, steel, LV_OPA_COVER, 2);
    rect(layer, center - 12, bottom + 10, center + 12, bottom + 14, outline, LV_OPA_COVER, 2);

    for (int p = 25; p <= 75; p += 25) {
        int32_t y = area->y1 + ui_tank_shape_level_y(h, (float)p);
        int32_t edge = area->x1 + ui_tank_shape_right_at(w, h, (float)p);
        line(layer, edge - 11, y, edge - 3, y, steel, LV_OPA_70, 1);
    }
}
