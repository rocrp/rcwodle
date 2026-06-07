/* WODLE-PORT: see WodleUsbCdc.h. Descriptors follow the SDK example
 * (cherryusb/device/cdc_acm) with wodle identity: VID 0x38F4, PID 0x1003
 * (HVR1 recovery = 0x1001, MSC transfer = 0x1002 — host tooling can tell
 * the modes apart).
 *
 * Thread model: cherryusb endpoint callbacks run at ISR level — the OUT
 * callback only copies into a ring buffer and re-arms the read; a worker
 * thread assembles lines and dispatches them through
 * WodleDebugCmds::runCommandLine (whose handlers only touch their mailbox).
 * TX is mutex-serialized (replies come from the worker thread, `wodle dump`
 * output from the main loop). */
#include "WodleUsbCdc.h"

#include <rtdevice.h>
#include <rtthread.h>

#include <cstring>

#include "WodleDebugCmds.h"
#include "bf0_hal.h"
#include "usbd_cdc_acm.h"
#include "usbd_core.h"

#define CDC_IN_EP 0x85
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x86

#define USBD_VID 0x38F4
#define USBD_PID 0x1003
#define USBD_MAX_POWER 100

#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)
#define CDC_MAX_MPS 64

#define CDC_RX_RING_LEN 512
#define CDC_LINE_LEN 160
#define CDC_TX_CHUNK 256
#define CDC_TX_TIMEOUT_MS 200

namespace
{
const uint8_t s_deviceDescriptor[] = {
    /* class EF/02/01 = composite (IAD), per the CDC template */
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01),
};

const uint8_t s_configDescriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02),
};

const uint8_t s_qualityDescriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

const char *s_stringDescriptors[] = {
    (const char[]){0x09, 0x04}, /* Langid: en-US */
    "hiveton",                  /* Manufacturer */
    "wodle Console",            /* Product */
    "WODLE0001",                /* Serial */
};

const uint8_t *deviceDescriptorCb(uint8_t) { return s_deviceDescriptor; }
const uint8_t *configDescriptorCb(uint8_t) { return s_configDescriptor; }
const uint8_t *qualityDescriptorCb(uint8_t) { return s_qualityDescriptor; }
const char *stringDescriptorCb(uint8_t, uint8_t index)
{
    if (index >= sizeof(s_stringDescriptors) / sizeof(s_stringDescriptors[0])) return nullptr;
    return s_stringDescriptors[index];
}

const struct usb_descriptor s_descriptor = {
    .device_descriptor_callback = deviceDescriptorCb,
    .config_descriptor_callback = configDescriptorCb,
    .device_quality_descriptor_callback = qualityDescriptorCb,
    .string_descriptor_callback = stringDescriptorCb,
};

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_readBuffer[CDC_MAX_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_writeBuffer[CDC_TX_CHUNK];

/* ISR -> worker ring */
uint8_t s_rxRing[CDC_RX_RING_LEN];
volatile uint16_t s_rxHead; /* ISR writes */
volatile uint16_t s_rxTail; /* worker reads */
struct rt_semaphore s_rxSem;

volatile bool s_txBusy;
volatile bool s_dtr;
bool s_started;
struct rt_mutex s_txLock;

struct usbd_interface s_intf0;
struct usbd_interface s_intf1;

struct usbd_endpoint s_outEp;
struct usbd_endpoint s_inEp;

void usbdEventHandler(uint8_t busid, uint8_t event)
{
    switch (event)
    {
    case USBD_EVENT_CONFIGURED:
        s_txBusy = false;
        usbd_ep_start_read(busid, CDC_OUT_EP, s_readBuffer, sizeof(s_readBuffer));
        break;
    case USBD_EVENT_DISCONNECTED:
        s_dtr = false;
        s_txBusy = false;
        break;
    default:
        break;
    }
}

void bulkOutCb(uint8_t busid, uint8_t, uint32_t nbytes)
{
    /* ISR level: stash and re-arm only. Ring overflow drops bytes (the
     * console protocol is line-oriented; a dropped char fails the command
     * loudly rather than wedging the endpoint). */
    for (uint32_t i = 0; i < nbytes; i++)
    {
        const uint16_t next = (uint16_t)((s_rxHead + 1) % CDC_RX_RING_LEN);
        if (next == s_rxTail) break;
        s_rxRing[s_rxHead] = s_readBuffer[i];
        s_rxHead = next;
    }
    usbd_ep_start_read(busid, CDC_OUT_EP, s_readBuffer, sizeof(s_readBuffer));
    rt_sem_release(&s_rxSem);
}

void bulkInCb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes)
    {
        usbd_ep_start_write(busid, CDC_IN_EP, RT_NULL, 0); /* ZLP */
    }
    else
    {
        s_txBusy = false;
    }
}

/* Blocking chunked write, thread context only. */
void cdcWrite(const char *text)
{
    if (!s_dtr || !text) return;
    rt_mutex_take(&s_txLock, RT_WAITING_FOREVER);
    size_t len = strlen(text);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(text);
    while (len > 0 && s_dtr)
    {
        const size_t count = len > CDC_TX_CHUNK ? CDC_TX_CHUNK : len;
        memcpy(s_writeBuffer, p, count);
        s_txBusy = true;
        if (usbd_ep_start_write(0, CDC_IN_EP, s_writeBuffer, count) < 0)
        {
            s_txBusy = false;
            break;
        }
        int waitMs = CDC_TX_TIMEOUT_MS;
        while (s_txBusy && waitMs-- > 0) rt_thread_mdelay(1);
        if (s_txBusy)
        {
            /* Host stopped draining — treat the port as gone so follow-up
             * writes (e.g. the remaining ~900 dump lines) don't each burn the
             * full timeout; DTR re-assert re-arms it (codex finding). */
            s_txBusy = false;
            s_dtr = false;
            rt_kprintf("[WodleUsbCdc] TX stalled — dropping console until DTR re-asserts\n");
            break;
        }
        p += count;
        len -= count;
    }
    rt_mutex_release(&s_txLock);
}

void rxThread(void *)
{
    char line[CDC_LINE_LEN];
    size_t pos = 0;

    while (true)
    {
        rt_sem_take(&s_rxSem, RT_WAITING_FOREVER);
        while (s_rxTail != s_rxHead)
        {
            const char ch = (char)s_rxRing[s_rxTail];
            s_rxTail = (uint16_t)((s_rxTail + 1) % CDC_RX_RING_LEN);

            if (ch == '\n' || ch == '\r' || pos == sizeof(line) - 1)
            {
                if (pos == 0) continue;
                line[pos] = '\0';
                pos = 0;
                WodleDebugCmds::runCommandLine(line);
                continue;
            }
            line[pos++] = ch;
        }
    }
}
} // namespace

/* cherryusb weak-override: host opened/closed the port. Only this TU defines
 * it (the MSC transfer mode has no CDC interface). */
extern "C" void usbd_cdc_acm_set_dtr(uint8_t, uint8_t, bool dtr)
{
    s_dtr = dtr;
}

namespace WodleUsbCdc
{

bool start()
{
    if (s_started) return true;

    rt_sem_init(&s_rxSem, "cdcrx", 0, RT_IPC_FLAG_FIFO);
    rt_mutex_init(&s_txLock, "cdctx", RT_IPC_FLAG_FIFO);

    usbd_desc_register(0, &s_descriptor);
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &s_intf0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &s_intf1));
    s_outEp.ep_addr = CDC_OUT_EP;
    s_outEp.ep_cb = bulkOutCb;
    s_inEp.ep_addr = CDC_IN_EP;
    s_inEp.ep_cb = bulkInCb;
    usbd_add_endpoint(0, &s_outEp);
    usbd_add_endpoint(0, &s_inEp);
    if (usbd_initialize(0, (uintptr_t)USBC_BASE, usbdEventHandler) < 0)
    {
        rt_kprintf("[WodleUsbCdc] usbd_initialize FAILED\n");
        return false;
    }

    rt_thread_t thread = rt_thread_create("cdccon", rxThread, RT_NULL, 2048, 22, 10);
    if (!thread)
    {
        rt_kprintf("[WodleUsbCdc] rx thread create FAILED\n");
        return false;
    }
    rt_thread_startup(thread);

    /* Mirror all wodle-command replies (incl. `wodle dump`) to the CDC port. */
    WodleDebugCmds::setReplySink(cdcWrite);

    s_started = true;
    rt_kprintf("[WodleUsbCdc] console up (VID 0x%04x PID 0x%04x)\n", USBD_VID, USBD_PID);
    return true;
}

bool connected() { return s_dtr; }

} // namespace WodleUsbCdc
