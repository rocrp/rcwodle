/* WODLE-PORT: USB mass-storage file transfer — exposes the SD card to a USB
 * host over the wodle's USB-C port (MUSB controller + cherryusb device
 * stack, modeled on SDK example cherryusb/device/msc/sdcard_disk).
 *
 * Mount exclusivity: FAT must never be mounted by the device and the host
 * at the same time. main.cpp guarantees this by entering MSC mode via a
 * silent restart and never calling Storage.begin() on that path — the
 * device side of the card stays untouched while the host owns it.
 *
 * start() blocks until the SD block device probes, registers the MSC
 * interface and brings up the USB device controller; sector IO then runs on
 * cherryusb's usbd_msc thread. There is no stop() — leaving MSC mode is a
 * restart (back into the normal mount-everything boot). */
#pragma once

namespace WodleUsbMsc
{

/* Returns false when the SD block device never became ready. */
bool start();

} // namespace WodleUsbMsc
