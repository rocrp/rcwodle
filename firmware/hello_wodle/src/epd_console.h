#pragma once

/* Line console rendered onto the e-paper — the board's only reliable output
 * channel (UART is signal-compromised, no USB stack yet). printf appends a
 * line (scrolls when full); flush renders + GC-refreshes (~3 s). */
void epd_console_init(void);
void epd_console_printf(const char *fmt, ...);
void epd_console_flush(void);
