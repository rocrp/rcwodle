#include "menu.h"
#include <string.h>

void menu_init(menu_t *menu, const char *title)
{
    memset(menu, 0, sizeof(menu_t));
    strncpy(menu->title, title, MENU_MAX_TITLE - 1);
    menu->visible_count = 10;
}

void menu_add_item(menu_t *menu, const char *title, menu_callback_t callback)
{
    if (menu->item_count >= MENU_MAX_ITEMS) return;
    menu_item_t *item = &menu->items[menu->item_count];
    strncpy(item->title, title, MENU_MAX_TITLE - 1);
    item->on_select = callback;
    menu->item_count++;
}

void menu_render(menu_t *menu, u8g2_t *u8g2)
{
    int w = u8g2_GetDisplayWidth(u8g2);
    int h = u8g2_GetDisplayHeight(u8g2);
    int item_h = 22;
    int title_h = 28;

    u8g2_ClearBuffer(u8g2);

    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    u8g2_DrawStr(u8g2, 4, title_h - 4, menu->title);
    u8g2_DrawLine(u8g2, 0, title_h, w - 1, title_h);

    u8g2_SetFont(u8g2, u8g2_font_helvR10_tf);

    int y = title_h + 4;
    for (int i = 0; i < menu->visible_count; i++) {
        int idx = menu->visible_start + i;
        if (idx >= menu->item_count) break;

        if (idx == menu->selected) {
            u8g2_SetDrawColor(u8g2, 1);
            u8g2_DrawBox(u8g2, 0, y, w, item_h);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawStr(u8g2, 6, y + 14, menu->items[idx].title);
            u8g2_SetDrawColor(u8g2, 1);
        } else {
            u8g2_DrawStr(u8g2, 6, y + 14, menu->items[idx].title);
        }
        y += item_h;
    }

    if (menu->item_count > menu->visible_count) {
        int bar_x = w - 8;
        int bar_h = h - title_h - 4;
        int thumb_h = bar_h * menu->visible_count / menu->item_count;
        int thumb_y = title_h + 4 + bar_h * menu->visible_start / menu->item_count;
        u8g2_DrawBox(u8g2, bar_x, title_h + 4, 6, bar_h);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawBox(u8g2, bar_x, thumb_y, 6, thumb_h);
        u8g2_SetDrawColor(u8g2, 1);
    }
}

void menu_handle_input(menu_t *menu, int action)
{
    switch (action) {
    case MENU_ACTION_UP:
        if (menu->selected > 0) {
            menu->selected--;
            if (menu->selected < menu->visible_start)
                menu->visible_start = menu->selected;
        }
        break;
    case MENU_ACTION_DOWN:
        if (menu->selected < menu->item_count - 1) {
            menu->selected++;
            if (menu->selected >= menu->visible_start + menu->visible_count)
                menu->visible_start = menu->selected - menu->visible_count + 1;
        }
        break;
    case MENU_ACTION_SELECT:
        if (menu->items[menu->selected].on_select)
            menu->items[menu->selected].on_select();
        break;
    case MENU_ACTION_BACK:
        break;
    }
}