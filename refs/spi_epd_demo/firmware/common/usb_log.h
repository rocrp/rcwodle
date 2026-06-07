#ifndef WODLE_USB_LOG_H
#define WODLE_USB_LOG_H

#include "rtthread.h"

typedef void (*usb_log_line_cb_t)(const char *line);

rt_err_t usb_log_init(usb_log_line_cb_t line_cb);
rt_bool_t usb_log_is_ready(void);
void usb_log_replay_history(void);
void usb_log_clear_history(void);
void usb_log_write(const char *text);
void usb_log_printf(const char *fmt, ...);

#endif
