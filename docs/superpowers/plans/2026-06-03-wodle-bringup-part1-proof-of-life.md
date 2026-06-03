# wodle bring-up Part 1: build pipeline + proof-of-life — Implementation Plan

> **Status: EXECUTED 2026-06-03** on branch `feat/wodle-eink-bringup`. All 5 tasks done; everything is
> build-verified (links @ `0x12218000`, `mk_update` tests pass). The only deferred step is the
> `[HARDWARE]` flash/observe in Task 4 (needs the device). **Deviations from the plan as written:**
> - Build output is **`build_wodle_hcpu/output/main.bin`**, not `bf0_ap.bin` (plan guessed the name).
> - A real blocker surfaced and was root-caused: the SDK board-walker double-linked `sf32lb52-lcd_base`
>   because `wodle` reused the stock `BSP_USING_BOARD_SF32LB52_LCD_N16R8` symbol. Fixed by giving wodle a
>   unique **`BSP_USING_BOARD_WODLE`** symbol + skipping the bootloader sub-build (Task 1/2).
> - `BSP_USING_KEY3` is not a real SDK symbol → PA44=KEY3 left as a board.conf comment, handled in app code.
> - `CO5300` kept enabled for S1 (panel probe is lazy → harmless); swap to UC8179C deferred to S3.
> - S1 frontlight is driven as raw GPIO (re-muxes off the SDK's auto-init PA1 PWM) + a UART `rt_kprintf`
>   heartbeat as a second channel.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up our own minimal RT-Thread firmware that builds for `board/wodle`, links @ `0x12218000`, and — when flashed — proves a custom app boots past the stock bootloader (frontlight heartbeat), plus the host-side tooling to package it for the SD `tf_ota` flash path.

**Architecture:** Clean-room app project in the repo (`firmware/hello_wodle/`) built against SiFli-SDK v2.5.0 + our `board/wodle` board def. No xiaozhi code, no LVGL. `refs/` and `~/w/_tmp/xiaozhi-sf32` are read-only references. S1 firmware: assert `PWR_EN` to hold power, blink the frontlight (`BL_PWM`), log over `rt_kprintf`. Host tool `mk_update.py` regenerates `update.json` (crc32+size) so the built `.bin` drops onto a recovery SD card.

**Tech Stack:** C / RT-Thread (SiFli BSP), SCons build (`scons --board=wodle`), arm-none-eabi-gcc 14.2.1 (`~/.sifli`), Python 3.13 + uv for `mk_update.py` (PEP-723 inline deps), pytest.

**Scope note:** This plan is Phase 0 + Stage S1 of `docs/superpowers/specs/2026-06-03-wodle-eink-validation-firmware-design.md`. S2 (buses/sensors) and S3 (UC8179C eink driver) are deliberately deferred to their own plans, written after S1 is hardware-verified (S1's "does it boot" verdict gates them).

**Environment prerequisite (run once per shell, not a task):**
```bash
source ~/w/_hw/SiFli-SDK/export.sh    # sets SIFLI_SDK + PATH to arm-none-eabi-gcc, sftool
```
Build dir for all firmware tasks: `firmware/hello_wodle/project/`. Build line: `scons --board=wodle -j8`.

---

## File Structure

Created in the repo:

| Path | Responsibility |
|---|---|
| `firmware/hello_wodle/project/SConstruct` | SDK build entry (verbatim SiFli template) |
| `firmware/hello_wodle/project/SConscript` | pulls in SDK SConscript + `../src` |
| `firmware/hello_wodle/project/rtconfig.py` | stub (board provides real rtconfig) |
| `firmware/hello_wodle/project/proj.conf` | app RT-Thread config (console/ulog/assert) |
| `firmware/hello_wodle/project/Kconfig` | app Kconfig root |
| `firmware/hello_wodle/project/Kconfig.proj` | app config (custom mem map) |
| `firmware/hello_wodle/src/main.c` | S1: power latch + frontlight heartbeat + `alive` MSH cmd |
| `firmware/hello_wodle/src/SConscript` | globs app `*.c` |
| `firmware/hello_wodle/README.md` | how to build + flash |
| `tools/mk_update.py` | regenerate `tf_ota` `update.json` from a built `.bin` |
| `tools/test_mk_update.py` | host unit tests for `mk_update.py` |

Modified:

| Path | Change |
|---|---|
| `board/wodle/hcpu/board.conf` | correct stale DevKit pins to schematic truth |
| `~/w/_hw/SiFli-SDK/customer/boards/wodle` | replace dir-copy with a symlink → repo `board/wodle` (single source of truth) |

---

## Task 1: App project scaffold that builds

Goal: a do-nothing app (boot banner + idle) that compiles and links at the stock app address. This isolates "does our project structure + board def + toolchain produce a valid image" from any peripheral code.

**Files:**
- Create: `firmware/hello_wodle/project/{SConstruct,SConscript,rtconfig.py,proj.conf,Kconfig,Kconfig.proj}`
- Create: `firmware/hello_wodle/src/{main.c,SConscript}`

- [ ] **Step 1: Create the project scaffold files**

`firmware/hello_wodle/project/SConstruct` (verbatim SiFli template):
```python
import os
import rtconfig

# Check SDK
SIFLI_SDK = os.getenv('SIFLI_SDK')
if not SIFLI_SDK:
    print("Please run export.sh in root folder of SIFLI SDK to set environment.")
    exit()
from building import *

# Prepare environment.
PrepareEnv()

# Add bootloader project
AddBootLoader(SIFLI_SDK, rtconfig.CHIP)

# Set default compile options
SifliEnv()

TARGET = rtconfig.OUTPUT_DIR + rtconfig.TARGET_NAME + '.' + rtconfig.TARGET_EXT
# Prepare building environment
objs = PrepareBuilding(None)
env = GetCurrentEnv()

# Build application.
DoBuilding(TARGET, objs)

# Add flash table build.
AddFTAB(SIFLI_SDK, rtconfig.CHIP)

# Generate download script
GenDownloadScript(env)
```

`firmware/hello_wodle/project/SConscript`:
```python
import os
from building import *

cwd = GetCurrentDir()
objs = []

# Add SDK
Import('SIFLI_SDK')
objs.extend(SConscript(os.path.join(SIFLI_SDK, 'SConscript'), variant_dir="sifli_sdk", duplicate=0))

# Add application source code
objs.extend(SConscript(cwd + '/../src/SConscript', variant_dir="src", duplicate=0))

Return('objs')
```

`firmware/hello_wodle/project/rtconfig.py` (intentionally a stub — the board's `hcpu/rtconfig.py` supplies `CHIP`/`TARGET_NAME`/`CORE`):
```python
# Stub. Real rtconfig is provided by board/wodle/hcpu/rtconfig.py via --board=wodle.
```

`firmware/hello_wodle/project/proj.conf`:
```conf
CONFIG_RT_MAIN_THREAD_STACK_SIZE=4096
CONFIG_RT_MAIN_THREAD_PRIORITY=15
CONFIG_BSP_USING_FULL_ASSERT=y
CONFIG_RT_USING_ULOG=y
CONFIG_ULOG_OUTPUT_THREAD_NAME=y
```

`firmware/hello_wodle/project/Kconfig`:
```kconfig
#Kconfig root for APP.
source "$SIFLI_SDK/Kconfig.v2"
rsource "Kconfig.proj"
```

`firmware/hello_wodle/project/Kconfig.proj`:
```kconfig
#APP specific configuration.
config CUSTOM_MEM_MAP
    bool
    select custom_mem_map
    default y
```

`firmware/hello_wodle/src/SConscript`:
```python
from building import *

# Add all app source
src = Glob('*.c')
group = DefineGroup('Applications', src, depend = [''])

Return('group')
```

`firmware/hello_wodle/src/main.c` (minimal boot banner; peripherals come in Task 4):
```c
#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"

int main(void)
{
    rt_kprintf("\n[hello_wodle] boot OK (scaffold). build %s %s\n", __DATE__, __TIME__);
    while (1)
    {
        rt_thread_mdelay(1000);
    }
    return 0;
}
```

- [ ] **Step 2: Build the scaffold**

Run (from repo root, env already sourced):
```bash
cd firmware/hello_wodle/project && scons --board=wodle -j8 2>&1 | tail -25
```
Expected: build completes; a `.bin` and ELF are produced under the project build/output dir. If `scons` errors on missing `middleware/bluetooth/Kconfig` or BT symbols, apply the known SDK fix from `docs/firmware-analysis.md` (stub the empty Kconfig; this is an SDK-side workaround, not an app change) and rebuild.

- [ ] **Step 3: Verify the link address (the critical static gate)**

Run:
```bash
ELF=$(ls firmware/hello_wodle/project/build_*/*.elf 2>/dev/null | head -1)
arm-none-eabi-readelf -l "$ELF" | grep -A1 LOAD | head
```
Expected: a LOAD segment **VirtAddr `0x12218000`** (the address the stock bootloader jumps to). If it links elsewhere (e.g. `0x12020000`), the board ptab/linker isn't being applied — stop and fix `board/wodle` selection before continuing.

- [ ] **Step 4: Commit**

```bash
git add firmware/hello_wodle
git commit -m "feat(fw): hello_wodle app scaffold builds + links @0x12218000"
```

---

## Task 2: Correct board.conf to schematic truth + single-source the board def

The SDK copy of the board is currently a duplicate of the repo's; edits must not diverge. Symlink it, then fix the stale DevKit pins the schematic overturned.

**Files:**
- Modify: `board/wodle/hcpu/board.conf`
- Modify (filesystem): `~/w/_hw/SiFli-SDK/customer/boards/wodle` → symlink

- [ ] **Step 1: Make the SDK board a symlink to the repo (one source of truth)**

```bash
SDKB=~/w/_hw/SiFli-SDK/customer/boards/wodle
# Safety: confirm the SDK copy has no unique edits vs the repo before replacing
diff -rq "$SDKB" /Users/rocry/w/_hw/rcwodle/board/wodle || echo "REVIEW DIFFS ABOVE before replacing"
rm -rf "$SDKB"
ln -s /Users/rocry/w/_hw/rcwodle/board/wodle "$SDKB"
ls -l ~/w/_hw/SiFli-SDK/customer/boards/wodle
```
Expected: a symlink pointing at the repo `board/wodle`. (If `diff` showed unique SDK-side edits, reconcile them into the repo copy first.)

- [ ] **Step 2: Edit `board/wodle/hcpu/board.conf` to match the schematic**

Apply these exact changes (see `refs/schematic/README.md`):
- `CONFIG_BSP_KEY2_PIN=11` → `CONFIG_BSP_KEY2_PIN=43`
- After the KEY2 block add KEY3:
  ```conf
  CONFIG_BSP_USING_KEY3=y
  CONFIG_BSP_KEY3_PIN=44
  CONFIG_BSP_KEY3_ACTIVE_HIGH=y
  ```
- `CONFIG_AW8155_GPIO_PIN=10` → `CONFIG_AW8155_GPIO_PIN=11`
- `CONFIG_BSP_CHARGER_INT_PIN=44` → `CONFIG_BSP_CHARGER_INT_PIN=41`
- Remove the two LED1 lines (`CONFIG_BSP_USING_LED1=y`, `CONFIG_BSP_LED1_PIN=26`, `CONFIG_BSP_LED1_ACTIVE_HIGH=y`) — wodle has no LED1 net.
- Change `CONFIG_LCD_USING_TFT_CO5300=y` → comment it out: `# CONFIG_LCD_USING_TFT_CO5300 is not set` (we drive UC8179C ourselves in S3; no SDK panel driver).
- Add a header comment line at the top:
  ```conf
  # Pins corrected to the official schematic 2026-06-03 (see refs/schematic/README.md).
  ```

> Note: `main.c` Task-1 uses no LED pin, so dropping LED1 won't break the build. If the SDK key middleware requires `KEY3` symbols not present in its Kconfig, drop the KEY3 lines (keep the comment) — keys aren't exercised until S2.

- [ ] **Step 3: Rebuild to confirm the board.conf still configures cleanly**

```bash
cd firmware/hello_wodle/project && scons --board=wodle -j8 2>&1 | tail -15
```
Expected: build succeeds (the pin-value changes are config data, not code).

- [ ] **Step 4: Commit**

```bash
git add board/wodle/hcpu/board.conf
git commit -m "fix(board): correct wodle pins to official schematic (KEY2/3, AW8155, charger INT, drop LED1/CO5300)"
```

---

## Task 3: `mk_update.py` — package a built `.bin` for the tf_ota SD flash path

Pure host tool, real TDD. It regenerates `update.json` (per-file crc32 + size + addr) from a built `hcpu_app.bin` so the recovery card boots it. Validated against the known-good stock manifest in `refs/card_snapshot_0246/firmware/update.json`.

**Files:**
- Create: `tools/mk_update.py`
- Create: `tools/test_mk_update.py`

- [ ] **Step 1: Write the failing test**

`tools/test_mk_update.py`:
```python
# /// script
# requires-python = ">=3.13"
# dependencies = ["pytest"]
# ///
import json
import zlib
from pathlib import Path

from mk_update import build_entry, build_manifest

STOCK = Path(__file__).parent.parent / "refs/card_snapshot_0246/firmware"


def test_build_entry_crc_and_size(tmp_path):
    data = b"hello wodle" * 1000
    f = tmp_path / "hcpu_app.bin"
    f.write_bytes(data)
    entry = build_entry(f, addr=0x12218000, region_size=0x240000, file_id=0)
    assert entry["size"] == len(data)
    assert entry["crc32"] == f"0x{zlib.crc32(data) & 0xFFFFFFFF:08X}"
    assert entry["addr"] == "0x12218000"
    assert entry["region_size"] == "0x00240000"


def test_matches_stock_manifest():
    """Regenerating from the stock bins must reproduce the stock crc32+size."""
    stock = json.loads((STOCK / "update.json").read_text())
    by_name = {e["name"]: e for e in stock["files"]}
    for name, addr, region in [
        ("hcpu_app.bin", 0x12218000, 0x240000),
        ("ezip_image.bin", 0x12460000, 0x680000),
        ("font_data.bin", 0x12AE0000, 0x400000),
    ]:
        got = build_entry(STOCK / name, addr=addr, region_size=region,
                          file_id=by_name[name]["file_id"])
        assert got["crc32"].lower() == by_name[name]["crc32"].lower(), name
        assert got["size"] == by_name[name]["size"], name
```

- [ ] **Step 2: Run it to confirm it fails**

Run:
```bash
cd tools && uv run --with pytest pytest test_mk_update.py -v
```
Expected: FAIL — `ModuleNotFoundError: No module named 'mk_update'`.

- [ ] **Step 3: Implement `mk_update.py`**

`tools/mk_update.py`:
```python
#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["typer"]
# ///
"""Regenerate a tf_ota update.json (crc32 + size + addr) from built firmware bins.

The wodle bootloader reads /firmware/update.json off the SD card, verifies each
file's crc32 + size, and only flashes if "version" is newer than what's installed.
"""
import json
import zlib
from pathlib import Path

import typer

# Stock region layout (mpi2 base 0x12000000) — see board/wodle/ptab.yaml.
REGIONS = {
    "hcpu_app.bin": (0x12218000, 0x240000, 0),
    "ezip_image.bin": (0x12460000, 0x680000, 1),
    "font_data.bin": (0x12AE0000, 0x400000, 2),
}

app = typer.Typer(add_completion=False)


def build_entry(path: Path, *, addr: int, region_size: int, file_id: int) -> dict:
    data = Path(path).read_bytes()
    return {
        "name": Path(path).name,
        "addr": f"0x{addr:08X}",
        "region_size": f"0x{region_size:08X}",
        "size": len(data),
        "crc32": f"0x{zlib.crc32(data) & 0xFFFFFFFF:08X}",
        "file_id": file_id,
    }


def build_manifest(firmware_dir: Path, version: str, generated_at: str) -> dict:
    files = []
    for name, (addr, region, fid) in REGIONS.items():
        p = firmware_dir / name
        if p.exists():
            files.append(build_entry(p, addr=addr, region_size=region, file_id=fid))
    return {"version": version, "generated_at": generated_at, "files": files}


@app.command()
def main(
    firmware_dir: Path = typer.Argument(..., help="dir holding the .bin(s)"),
    version: str = typer.Option(..., "--version", "-v", help='e.g. "V1.4.0.9001"'),
    generated_at: str = typer.Option("1970-01-01T00:00:00Z", "--at"),
    out: Path = typer.Option(None, "--out", "-o", help="default: <firmware_dir>/update.json"),
):
    manifest = build_manifest(firmware_dir, version, generated_at)
    if not manifest["files"]:
        raise typer.BadParameter(f"no known bins found in {firmware_dir}")
    out = out or (firmware_dir / "update.json")
    out.write_text(json.dumps(manifest, indent=2, ensure_ascii=False))
    typer.echo(f"wrote {out} ({len(manifest['files'])} files, version {version})")


if __name__ == "__main__":
    app()
```

- [ ] **Step 4: Run the tests to confirm they pass**

Run:
```bash
cd tools && uv run --with pytest pytest test_mk_update.py -v
```
Expected: both tests PASS (the stock-manifest test proves our crc32/size logic matches the bootloader's expectation).

- [ ] **Step 5: Commit**

```bash
git add tools/mk_update.py tools/test_mk_update.py
git commit -m "feat(tools): mk_update.py — regenerate tf_ota update.json from built bins"
```

---

## Task 4: S1 firmware — power latch + frontlight heartbeat + `alive` command

Now make the app actually exercise hardware: hold the power rail, blink the frontlight (naked-eye proof-of-life), and add an MSH command for when a UART is tapped. Keep it dead simple — blink first; PWM "breathe" is a later refinement.

**Files:**
- Modify: `firmware/hello_wodle/src/main.c`

- [ ] **Step 1: Replace `main.c` with the S1 implementation**

`firmware/hello_wodle/src/main.c`:
```c
#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"

/* wodle pin map (PAxx -> pad index), from refs/schematic/README.md */
#define WODLE_PWR_EN_PIN   10   /* PA10 system power-enable latch (assert high to stay on) */
#define WODLE_BL_PWM_PIN    1   /* PA1  frontlight (GPIO blink in S1; PWM breathe later)   */

static rt_uint32_t s_uptime_s;

/* MSH: `alive` — confirm the shell is live and report uptime (for a UART tap) */
static void cmd_alive(int argc, char **argv)
{
    rt_kprintf("alive: up %u s, heartbeat running\n", s_uptime_s);
}
MSH_CMD_EXPORT(cmd_alive, wodle: report liveness);

int main(void)
{
    rt_kprintf("\n[hello_wodle] S1 boot: %s %s\n", __DATE__, __TIME__);

    /* 1) Hold the power latch FIRST — a soft-power device may self-off otherwise. */
    rt_pin_mode(WODLE_PWR_EN_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(WODLE_PWR_EN_PIN, PIN_HIGH);
    rt_kprintf("[hello_wodle] PWR_EN(PA10) asserted\n");

    /* 2) Frontlight as proof-of-life: blink ~1 Hz, visible to the naked eye. */
    rt_pin_mode(WODLE_BL_PWM_PIN, PIN_MODE_OUTPUT);

    rt_bool_t on = RT_FALSE;
    while (1)
    {
        on = !on;
        rt_pin_write(WODLE_BL_PWM_PIN, on ? PIN_HIGH : PIN_LOW);
        if (on) s_uptime_s += 1;           /* count full on/off cycles ~= seconds */
        rt_thread_mdelay(500);
    }
    return 0;
}
```

> Pin-number caveat: the SiFli BSP addresses GPIO by pad index; `board.conf` uses raw PA numbers
> (`KEY1_PIN=34`), so PA10→10 / PA1→1 here. If the frontlight doesn't respond on hardware, confirm the
> BSP's pad macro (some BSPs need `GET_PIN`)/active-polarity and adjust — this is a Step-4 HIL check.

- [ ] **Step 2: Build**

```bash
cd firmware/hello_wodle/project && scons --board=wodle -j8 2>&1 | tail -15
```
Expected: builds; produces `bf0_ap.bin` (the HCPU app) under the build output dir.

- [ ] **Step 3: Re-verify link address + commit**

```bash
ELF=$(ls firmware/hello_wodle/project/build_*/*.elf 2>/dev/null | head -1)
arm-none-eabi-readelf -l "$ELF" | grep -A1 LOAD | head
git add firmware/hello_wodle/src/main.c
git commit -m "feat(fw): S1 proof-of-life — PWR_EN latch + frontlight heartbeat + alive cmd"
```
Expected: LOAD @ `0x12218000`; commit succeeds.

- [ ] **Step 4: [HARDWARE] Flash + observe — the make-or-break (run when device is in hand)**

Package and flash via the proven SD path:
```bash
# locate the built app bin
APP=$(ls firmware/hello_wodle/project/build_*/bf0_ap.bin 2>/dev/null | head -1)
mkdir -p /tmp/wodle_fw && cp "$APP" /tmp/wodle_fw/hcpu_app.bin
uv run tools/mk_update.py /tmp/wodle_fw --version V1.4.0.9001 --at 2026-06-03T00:00:00Z
# copy /tmp/wodle_fw/{hcpu_app.bin,update.json} to the recovery SD card /firmware/, insert, power on
```
(Keep `refs/card_snapshot_0246/firmware/` as the restore-to-stock set.) Alternative channel: HVR1 USB —
`uv run tools/wodle_flash.py write "$APP" --addr 0x12218000` after entering recovery.

**Pass = the frontlight blinks** → a custom SDK-v2.5.0 app boots past the stock bootloader (secure-boot
does not gate the app jump; clock/PSRAM init is compatible). **Fail = no blink** → capture any UART
output on PA18/19, restore stock from the snapshot, and the verdict (secboot/boot-compat blocker) decides
whether S2/S3 proceed or we pivot to SWD/signing. Record the outcome in `docs/framework_plan.md`.

---

## Task 5: Document the bring-up runbook

**Files:**
- Create: `firmware/hello_wodle/README.md`
- Modify: `README.md` (link the firmware), `docs/framework_plan.md` (point the "no-wire dev cycle" at `mk_update.py`)

- [ ] **Step 1: Write `firmware/hello_wodle/README.md`**

Content: one-paragraph purpose (S1 proof-of-life), the env line (`source ~/w/_hw/SiFli-SDK/export.sh`),
the build line (`cd project && scons --board=wodle -j8`), the link-check command, and the two flash
recipes (SD `tf_ota` via `mk_update.py`; HVR1 via `wodle_flash.py`) with the restore-to-stock note.

- [ ] **Step 2: Link it from the top-level docs**

In `README.md`, add a row to the docs/tools table pointing to `firmware/hello_wodle/`. In
`docs/framework_plan.md`, update the "no-wire dev cycle" code block to call
`uv run tools/mk_update.py <dir> --version …` instead of the prose "bump version + crc32 by hand".

- [ ] **Step 3: Commit**

```bash
git add firmware/hello_wodle/README.md README.md docs/framework_plan.md
git commit -m "docs(fw): hello_wodle bring-up runbook + wire mk_update into the dev loop"
```

---

## Self-Review

**Spec coverage (Phase 0 + S1 portion of the design spec):**
- Build-verified app on `board/wodle` linking @ `0x12218000` → Tasks 1, 4 (link-address gate).
- board.conf corrected to schematic truth → Task 2.
- S1 proof-of-life (PWR_EN latch + frontlight heartbeat + log + MSH) → Task 4.
- Flashing via SD `tf_ota` with `mk_update.py` (+ HVR1 alt) → Tasks 3, 4 Step 4.
- Observability (frontlight + `rt_kprintf`/UART) → Task 4.
- S2 (buses/sensors), S3 (UC8179C driver), PWM "breathe", hardware-LCDC SPI, 4-gray → **explicitly deferred** to later plans (spec "Out of scope (v1)" + this plan's scope note). No gap.

**Placeholder scan:** all file contents are concrete; the only "fill-in" is Task 5 Step 1 README prose, which is fully specified by its bullet list. The pin-number and BT-Kconfig caveats are real HIL/SDK contingencies with stated fallbacks, not placeholders.

**Type/name consistency:** `build_entry`/`build_manifest` signatures match between `mk_update.py` and `test_mk_update.py`; pin macros `WODLE_PWR_EN_PIN`/`WODLE_BL_PWM_PIN` defined and used in the same file; build output names (`bf0_ap.bin` from board `TARGET_NAME='bf0_ap'`, `*.elf`) consistent across Tasks 1/4/5.

**Risk acknowledged in-plan:** Task 1 Step 2 (BT Kconfig SDK fix), Task 2 Step 2 (KEY3 Kconfig fallback), Task 4 Step 1 (pad-number/polarity), Task 4 Step 4 (the secboot verdict) — each with a concrete fallback.
