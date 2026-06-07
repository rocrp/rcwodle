#ifndef U8G2_PORT_H
#define U8G2_PORT_H

#include "u8g2.h"

void u8g2_port_init(u8g2_t *u8g2);
void u8g2_port_flush(u8g2_t *u8g2, uint8_t *epd_fb);

#endif