#ifndef BACKLIGHT_H
#define BACKLIGHT_H

void backlight_init(void);
void backlight_set(int percent);  /* 0-100 */
int  backlight_get(void);

#endif
