/* WODLE-PORT: USB-CDC console — the `wodle` debug commands over the charge
 * cable. Normal boots only (the USB-MSC file-transfer mode is its own boot
 * path and owns the controller there); enumerates as a serial port
 * (VID 0x38F4 PID 0x1003, /dev/cu.usbmodem* on macOS) the moment the cable
 * is plugged. Replies — including the full `wodle dump` framebuffer stream —
 * are mirrored to the CDC port, so tools/wodle_console.py works without the
 * WCH-Link UART. finsh stays on uart1. */
#pragma once

namespace WodleUsbCdc
{
/* Bring up the CDC device. Safe to call when USB is disconnected — the host
 * just enumerates whenever the cable arrives. */
bool start();
/* True once the host opened the port (DTR asserted). */
bool connected();
} // namespace WodleUsbCdc
