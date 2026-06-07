#include "usb_log.h"

#include "rtdevice.h"
#include "drivers/usb_device.h"
#include <stdarg.h>

#define USB_LOG_RX_BUF_LEN 128
#define USB_LOG_PRINTF_BUF_LEN 192
#define USB_LOG_HISTORY_LEN 4096
#define USB_LOG_REPLAY_CHUNK_LEN 96

static rt_device_t s_vcom;
static struct rt_semaphore s_rx_sem;
static struct rt_mutex s_history_lock;
static usb_log_line_cb_t s_line_cb;
static rt_bool_t s_started;
static rt_bool_t s_history_ready;
static char s_history[USB_LOG_HISTORY_LEN];
static rt_size_t s_history_len;
static char s_replay_chunk[USB_LOG_REPLAY_CHUNK_LEN];

static rt_err_t usb_log_rx_ind(rt_device_t dev, rt_size_t size)
{
    (void)dev;
    (void)size;

    rt_sem_release(&s_rx_sem);
    return RT_EOK;
}

static void history_append(const char *text, rt_size_t len)
{
    if (!text || len == 0 || !s_history_ready)
    {
        return;
    }

    rt_mutex_take(&s_history_lock, RT_WAITING_FOREVER);

    if (len >= sizeof(s_history))
    {
        text += len - (sizeof(s_history) - 1);
        len = sizeof(s_history) - 1;
        s_history_len = 0;
    }
    else if (s_history_len + len >= sizeof(s_history))
    {
        rt_size_t drop = (s_history_len + len) - (sizeof(s_history) - 1);
        rt_memmove(s_history, s_history + drop, s_history_len - drop);
        s_history_len -= drop;
    }

    rt_memcpy(s_history + s_history_len, text, len);
    s_history_len += len;
    s_history[s_history_len] = '\0';

    rt_mutex_release(&s_history_lock);
}

static void usb_log_rx_thread(void *parameter)
{
    (void)parameter;

    char ch;
    char line[USB_LOG_RX_BUF_LEN];
    rt_size_t pos = 0;
    rt_bool_t line_ended = RT_TRUE;

    while (1)
    {
        while (rt_device_read(s_vcom, -1, &ch, 1) != 1)
        {
            rt_sem_take(&s_rx_sem, RT_WAITING_FOREVER);
        }

        if (ch == '\r' || ch == '\n' || pos == sizeof(line) - 1)
        {
            if (line_ended && pos == 0)
            {
                continue;
            }

            line[pos] = '\0';

            if (rt_strcmp(line, "history") == 0)
            {
                usb_log_replay_history();
                pos = 0;
                line_ended = RT_TRUE;
                continue;
            }

            if (rt_strcmp(line, "clear-history") == 0)
            {
                usb_log_clear_history();
                usb_log_write("[usb_log] history cleared\r\n");
                pos = 0;
                line_ended = RT_TRUE;
                continue;
            }

            if (s_line_cb)
            {
                s_line_cb(line);
            }

            pos = 0;
            line_ended = RT_TRUE;
            continue;
        }

        line_ended = RT_FALSE;
        line[pos++] = ch;
    }
}

rt_err_t usb_log_init(usb_log_line_cb_t line_cb)
{
    s_line_cb = line_cb;

    if (!s_history_ready)
    {
        rt_mutex_init(&s_history_lock, "usblogh", RT_IPC_FLAG_FIFO);
        s_history_ready = RT_TRUE;
    }

    if (s_started)
    {
        return RT_EOK;
    }

    s_vcom = rt_device_find("vcom");
    if (!s_vcom)
    {
        rt_kprintf("[usb_log] vcom device not found\r\n");
        return -RT_ERROR;
    }

    rt_device_init(s_vcom);

    rt_err_t err = rt_device_open(s_vcom,
                                  RT_DEVICE_FLAG_RDWR |
                                  RT_DEVICE_FLAG_INT_RX |
                                  RT_DEVICE_FLAG_DMA_TX |
                                  RT_DEVICE_FLAG_STREAM);
    if (err != RT_EOK && err != -RT_EBUSY)
    {
        rt_kprintf("[usb_log] open vcom failed: %d\r\n", err);
        return err;
    }

    rt_sem_init(&s_rx_sem, "usblogrx", 0, RT_IPC_FLAG_FIFO);
    rt_device_set_rx_indicate(s_vcom, usb_log_rx_ind);

    rt_thread_t thread = rt_thread_create("usblog_rx",
                                          usb_log_rx_thread,
                                          RT_NULL,
                                          1024,
                                          25,
                                          10);
    if (!thread)
    {
        rt_kprintf("[usb_log] create rx thread failed\r\n");
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    s_started = RT_TRUE;
    return RT_EOK;
}

rt_bool_t usb_log_is_ready(void)
{
    return s_vcom != RT_NULL;
}

void usb_log_replay_history(void)
{
    if (!s_vcom || !s_history_ready)
    {
        return;
    }

    rt_mutex_take(&s_history_lock, RT_WAITING_FOREVER);

    const char *begin = "[usb_log] history begin\r\n";
    const char *end = "[usb_log] history end\r\n";

    rt_device_write(s_vcom, 0, begin, rt_strlen(begin));
    rt_size_t offset = 0;
    rt_size_t history_len = s_history_len;

    while (offset < history_len)
    {
        rt_size_t chunk_len = history_len - offset;
        if (chunk_len > sizeof(s_replay_chunk))
        {
            chunk_len = sizeof(s_replay_chunk);
        }

        rt_memcpy(s_replay_chunk, s_history + offset, chunk_len);
        rt_mutex_release(&s_history_lock);

        rt_device_write(s_vcom, 0, s_replay_chunk, chunk_len);
        rt_thread_mdelay(2);

        rt_mutex_take(&s_history_lock, RT_WAITING_FOREVER);
        offset += chunk_len;
    }

    rt_device_write(s_vcom, 0, end, rt_strlen(end));

    rt_mutex_release(&s_history_lock);
}

void usb_log_clear_history(void)
{
    if (!s_history_ready)
    {
        return;
    }

    rt_mutex_take(&s_history_lock, RT_WAITING_FOREVER);
    s_history_len = 0;
    s_history[0] = '\0';
    rt_mutex_release(&s_history_lock);
}

void usb_log_write(const char *text)
{
    if (!s_vcom || !text)
    {
        return;
    }

    rt_size_t len = rt_strlen(text);
    history_append(text, len);
    rt_device_write(s_vcom, 0, text, len);
}

void usb_log_printf(const char *fmt, ...)
{
    char buf[USB_LOG_PRINTF_BUF_LEN];
    va_list args;

    va_start(args, fmt);
    rt_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    usb_log_write(buf);
}
