# Hiveton USB Recovery (Qt)

Vendor reference flasher for the wodle board (`sf32lb52-lcd_n16r8`). Qt6 GUI that
speaks the **HVR1** recovery protocol over the USB-CDC port that appears
(`/dev/cu.usbmodem*`) after the device enters USB recovery mode.

This is the upstream implementation that [`../wodle_usbrec.py`](../wodle_usbrec.py)
and [`../fw_usbrec.py`](../fw_usbrec.py) reverse-engineered — ground truth for the
frame format, command set, and status codes. See [`../../docs/firmware-analysis.md`](../../docs/firmware-analysis.md).

Protocol constants (from `main.cpp`): magic `0x31525648` "HVR1", version 1,
VID `0x38f4` / PID `0x1001`, max payload 2048 B. Commands: HELLO BEGIN ERASE
WRITE VERIFY COMMIT REBOOT. Status: OK BAD_FRAME DENIED FLASH_ERROR CRC_ERROR BAD_PARTITION.

## Build (macOS)

Deps: `cmake` + Qt6 (`qtbase`, `qtserialport`).

```sh
brew install cmake qtbase qtserialport

cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase);$(brew --prefix qtserialport)"
cmake --build build -j
```

Homebrew's Qt6 modules are keg-only and each lives in its own prefix, so both
`qtbase` and `qtserialport` must be on `CMAKE_PREFIX_PATH`.

## Run

```sh
open build/hiveton-usb-recovery.app
```

Put the device in USB recovery mode first (Settings → "USB recovery", or finsh
`usb_recovery_mode`), then select the partitions/images and flash from the GUI.
