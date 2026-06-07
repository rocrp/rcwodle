#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include "u8g2.h"

#define MENU_MAX_ITEMS  12
#define MENU_MAX_TITLE   24

typedef void (*menu_callback_t)(void);

typedef struct {
    char title[MENU_MAX_TITLE];
    menu_callback_t on_select;
} menu_item_t;

typedef struct {
    char title[MENU_MAX_TITLE];
    menu_item_t items[MENU_MAX_ITEMS];
    uint8_t item_count;
    uint8_t selected;
    uint8_t visible_start;
    uint8_t visible_count;
} menu_t;

void menu_init(menu_t *menu, const char *title);
void menu_add_item(menu_t *menu, const char *title, menu_callback_t callback);
void menu_render(menu_t *menu, u8g2_t *u8g2);
void menu_handle_input(menu_t *menu, int action);

#define MENU_ACTION_UP     1
#define MENU_ACTION_DOWN   2
#define MENU_ACTION_SELECT 3
#define MENU_ACTION_BACK   4

#endif