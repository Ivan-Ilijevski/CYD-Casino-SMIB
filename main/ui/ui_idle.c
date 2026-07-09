// Idle (unauthenticated) screen: the original bring-up screen — white
// background, card prompt on top, club logo centered.
#include "drivers/nfc.h" // CARD_PROMPT_TEXT
#include "ui_internal.h"

lv_obj_t *ui_idle_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(scr);
    lv_obj_set_style_text_font(label, &Sf_Compact_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(label, CARD_PROMPT_TEXT);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);

    LV_IMG_DECLARE(royal_club_zelenezburg_logo);
    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &royal_club_zelenezburg_logo);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, 10);

    return scr;
}
