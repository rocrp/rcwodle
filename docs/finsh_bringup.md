# wodle finsh console — bring-up cheat-sheet

The wodle's only interactive shell is **RT-Thread finsh on USART1**. The official schematic
(`../refs/schematic/`) already closed the pin map, EPD controller (UC8179C), and I²C parts — so this
console run is now mostly **confirmation** (the EPD **BUSY** line + exact **TRES**, plus boot logs for a
custom app), not discovery. ~2 wires, ~5 min.

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

# --- pin map (schematic-confirmed; this just cross-checks + finds the EPD BUSY net) ---
pin                         # dump all configured GPIOs  (if it needs args: `help pin`)
regop 0x50003000 64         # raw PINMUX1 register block (decode pad->func offline)

# --- display: confirm UC8179C + resolution + the BUSY line ---
lcd_rreg                    # read panel ID registers  (try `lcd_rreg 0x04` / `help lcd_rreg`)
epd_stat
epd_test                    # / drv_lcd_test — draws a test pattern (confirms panel + BUSY work)

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
- `pin` / `regop` → cross-checks the schematic pin map + identifies the **EPD BUSY** pin (likely PA02/TE).
- `lcd_rreg` / `epd_stat` / `epd_test` → confirms the **UC8179C** + the exact TRES (792×528) against the panel.
- `i2c` scan → confirms **CST836U @0x15** (I²C1), AW32001 @0x49 + BQ27220 @0x55 (I²C2).
- `fal` → exact partition table → confirm/refine `board/wodle/ptab.yaml`.

With the EPD controller (UC8179C) + full schematic pins already in hand, a **minimal custom app
(boot + draw to e-paper)** is a real, testable target — this run just de-risks the BUSY/TRES details.
