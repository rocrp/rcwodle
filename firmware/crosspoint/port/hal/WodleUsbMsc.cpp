/* WODLE-PORT: see WodleUsbMsc.h. Descriptors + sd0 backend follow the SDK
 * example (cherryusb/device/msc/sdcard_disk) with wodle identity: hiveton
 * VID 0x38F4 (the HVR1 recovery CDC uses PID 0x1001 — MSC takes 0x1002 so
 * host-side tooling can tell the modes apart). */
#include "WodleUsbMsc.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "bf0_hal.h"
#include "usbd_core.h"
#include "usbd_msc.h"

#define MSC_IN_EP 0x85
#define MSC_OUT_EP 0x02

#define USBD_VID 0x38F4
#define USBD_PID 0x1002
#define USBD_MAX_POWER 100

#define USB_CONFIG_SIZE (9 + MSC_DESCRIPTOR_LEN)
#define MSC_MAX_MPS 64

#define SD_DEVNAME "sd0"
#define SD_READY_TIMEOUT_MS 5000
#define SD_READY_RETRY_MS 100

namespace
{
const uint8_t s_deviceDescriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0100, 0x01),
};

const uint8_t s_configDescriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, MSC_MAX_MPS, 0x02),
};

const uint8_t s_qualityDescriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01),
};

const char *s_stringDescriptors[] = {
    (const char[]){0x09, 0x04}, /* Langid: en-US */
    "hiveton",                  /* Manufacturer */
    "wodle SD Card",            /* Product */
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

void usbdEventHandler(uint8_t, uint8_t) {}

rt_device_t s_dev;
uint32_t s_blockSize;
uint32_t s_blockNum;

struct usbd_interface s_intf;

extern "C" int rt_spi_msd_init(void);

bool openSdBlockDevice()
{
    const rt_tick_t deadline = rt_tick_get() + rt_tick_from_millisecond(SD_READY_TIMEOUT_MS);
    while (rt_tick_get() < deadline)
    {
        if (!s_dev)
        {
            if (rt_device_find(SD_DEVNAME) == RT_NULL) (void)rt_spi_msd_init();
            s_dev = rt_device_find(SD_DEVNAME);
        }
        if (s_dev)
        {
            if (rt_device_open(s_dev, RT_DEVICE_OFLAG_RDWR) == RT_EOK)
            {
                struct rt_device_blk_geometry geometry;
                if (rt_device_control(s_dev, RT_DEVICE_CTRL_BLK_GETGEOME, &geometry) == RT_EOK &&
                    geometry.bytes_per_sector > 0 && geometry.sector_count > 0 &&
                    (CONFIG_USBDEV_MSC_MAX_BUFSIZE % geometry.bytes_per_sector) == 0)
                {
                    s_blockSize = geometry.bytes_per_sector;
                    s_blockNum = geometry.sector_count;
                    rt_kprintf("[WodleUsbMsc] %s ready: %u blocks x %u B\n", SD_DEVNAME,
                               (unsigned)s_blockNum, (unsigned)s_blockSize);
                    return true;
                }
                rt_device_close(s_dev);
                s_dev = RT_NULL;
            }
            else
            {
                s_dev = RT_NULL;
            }
        }
        rt_thread_mdelay(SD_READY_RETRY_MS);
    }
    rt_kprintf("[WodleUsbMsc] %s not ready, giving up\n", SD_DEVNAME);
    return false;
}
} // namespace

/* cherryusb MSC backend hooks (C linkage expected by usbd_msc.c) */
extern "C" void usbd_msc_get_cap(uint8_t, uint8_t, uint32_t *block_num, uint32_t *block_size)
{
    *block_num = s_blockNum;
    *block_size = s_blockSize;
}

extern "C" int usbd_msc_sector_read(uint8_t, uint8_t, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    if (!s_dev || s_blockSize == 0 || (length % s_blockSize) != 0) return -1;
    const uint32_t sectors = length / s_blockSize;
    return rt_device_read(s_dev, sector, buffer, sectors) == (rt_size_t)sectors ? 0 : -1;
}

extern "C" int usbd_msc_sector_write(uint8_t, uint8_t, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    if (!s_dev || s_blockSize == 0 || (length % s_blockSize) != 0) return -1;
    const uint32_t sectors = length / s_blockSize;
    return rt_device_write(s_dev, sector, buffer, sectors) == (rt_size_t)sectors ? 0 : -1;
}

namespace WodleUsbMsc
{

bool start()
{
    if (!openSdBlockDevice()) return false;

    usbd_desc_register(0, &s_descriptor);
    usbd_add_interface(0, usbd_msc_init_intf(0, &s_intf, MSC_OUT_EP, MSC_IN_EP));
    usbd_initialize(0, (uintptr_t)USBC_BASE, usbdEventHandler);
    rt_kprintf("[WodleUsbMsc] USB device up (VID 0x%04x PID 0x%04x)\n", USBD_VID, USBD_PID);
    return true;
}

} // namespace WodleUsbMsc
