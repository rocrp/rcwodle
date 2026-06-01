# wodle finsh console — bring-up cheat-sheet

The wodle's only interactive shell is **RT-Thread finsh on USART1**. This is the fastest way to
close every remaining hardware unknown. ~2 wires, ~5 min.

## Wiring (USB-UART ↔ wodle)

3.3 V USB-UART adapter (CP2102 / CH340 / FT232 — **must be 3.3 V logic**, NOT 5 V):

| USB-UART | wodle pad | note |
|---|---|---|
| RX  | **PA19** (TXD) | adapter RX ← device TX |
| TX  | **PA18** (RXD) | adapter TX → device RX |
| GND | GND | common ground required |
| (no Vcc) | — | power the wodle from its own battery/USB; don't back-power |

PA18/PA19 are the SWD/debug pads — look for a test-point pair or a small header near the SoC.
Keep PA34 (power key) pull-down untouched.

## Connect (on the Mac)

The adapter shows up as `/dev/cu.usb*`. Try **1000000** baud first, then 115200:
```
ls /dev/cu.usb*
# screen (Ctrl-A then K to quit) or:
python3 -m serial.tools.miniterm /dev/cu.usbserial-XXXX 1000000
```
If you see boot logs but the shell ignores input, finsh may want a password — try **`rtthread`**.

## Commands to run (capture ALL output, paste back to me)

```sh
# --- identity / sanity ---
version
list_device                 # every registered driver = peripheral inventory

# --- THE pin map (resolves EPD BUSY/DC, touch INT/RST among PA21/33/38/42/43) ---
pin                         # dump all configured GPIOs  (if it needs args: `help pin`)
regop 0x50003000 64         # raw PINMUX1 register block (decode pad->func offline)

# --- display: identify the EPD controller + resolution ---
lcd_rreg                    # read panel ID registers  (try `lcd_rreg 0x04` / `help lcd_rreg`)
epd_stat
drv_lcd_test                # optional: draws a test pattern (confirms panel works)

# --- I2C: confirm chip addresses + bus assignment ---
help i2c                    # find the scan subcommand, then scan i2c1 and i2c2
list_device                 # (shows i2c1/i2c2 + attached devices)

# --- storage / partition truth ---
fal                         # real FAL partition table
nvds                        # / kvdb_debug — settings store

# --- power telemetry ---
power                       # AW32001 + BQ27220 live state
```

## What each result unlocks
- `pin` / `regop` → completes the pinmux (the last 5 unknown GPIOs) → finalize `recovered_pinmux.c`.
- `lcd_rreg` / `epd_stat` → **EPD controller part** → I can write the display driver.
- `i2c` scan → confirms CST816 @0x15, AW32001 @0x49, BQ27220 @0x55 on their buses.
- `fal` → exact partition table → confirm/refine `board/wodle/ptab.yaml`.

Once we have the EPD controller + full pins, a **minimal custom app (boot + draw to e-paper)** is a
real, testable target.
