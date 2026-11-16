#include "app_widgets.h"
#include "tokens.h"

#define BUTTON_H 44

lv_obj_t *widget_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, lv_align_t align,
                       int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    lv_obj_align(l, align, 0, y);
    return l;
}

lv_obj_t *widget_circle(lv_obj_t *parent, int dia, uint32_t rgb)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, dia, dia);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(rgb), 0);
    lv_obj_set_clickable(c, false);
    return c;
}

lv_obj_t *widget_button(lv_obj_t *parent, const char *text, int w, int x, int y)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, BUTTON_H);
    lv_obj_align(b, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_radius(b, BUTTON_H / 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(HUD_PANEL), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(HUD_LINE), LV_STATE_PRESSED);   // press feedback
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(HUD_LINE), 0);
    lv_obj_set_clickable(b, true);
    lv_obj_set_scrollable(b, false);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_t *l = widget_label(b, LV_FONT_DEFAULT, HUD_INK, LV_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    return b;
}

void widget_dots_create(lv_obj_t *parent, lv_obj_t *dots[WIDGET_STRENGTH_MAX], int dia, int pitch,
                        lv_align_t align, int x, int y)
{
    for (int i = 0; i < WIDGET_STRENGTH_MAX; i++) {
        dots[i] = widget_circle(parent, dia, HUD_OFF);
        lv_obj_align(dots[i], align, x + (i - WIDGET_STRENGTH_MAX / 2) * pitch, y);
    }
}

void widget_dots_set(lv_obj_t *const dots[WIDGET_STRENGTH_MAX], int strength, uint32_t rgb)
{
    for (int i = 0; i < WIDGET_STRENGTH_MAX; i++)
        lv_obj_set_style_bg_color(dots[i], lv_color_hex(i < strength ? rgb : HUD_OFF), 0);
}
