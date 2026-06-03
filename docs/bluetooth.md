# Talking to the wodle over Bluetooth — feasibility

Goal was a Mac CLI that talks to the device like the official app, no wires.

## Verdict: no local BLE control path. Use UART or USB-CDC instead.

A macOS **BLE** CLI "like the app" is **not viable**, for two independent reasons:

1. **There is no local BLE control service to talk to.** Verified in *both* firmware images — the HCPU
   app and the BT-core image (`dfu_pan.bin`): **zero custom 128-bit UUIDs**, no NUS/serial/FExx/FFxx
   service. Only `appm_add_svc` registering **standard** services (GAP/GATT/DIS/Battery) + GAP pairing
   + GATT **client** role. The one custom UUID `258EAFA5-…` is a **WebSocket token**, not a GATT
   service; `sible`/"Sifli BLE command" is a **BLE RF-test** tool (`enter_dut`/`tx_test`), not a control
   API. **[C-RE]**
2. **The app's link isn't a BLE command protocol anyway.** The app pairs over **BT-Classic** (A2DP/
   AVRCP/HFP, with iPhone `+IPHONEACCEV`/`+BIND`) and **shares the phone's internet over BT-PAN**; the
   device then does everything against the **XiaoZhi cloud WebSocket**. "Like the app" = *be the
   phone's internet + the cloud*, not send the device commands. **macOS can't script BT-PAN-NAP or
   HFP-AG** roles, so a CLI can't fill the app's role. **[C-RE]**

Plus a physical gate: the device is **bonded to the phone and not advertising/discoverable**, so
nothing — including the official app — can connect to it without a first-time pairing step.

## Reachability — exhaustively tested (2026-06-01)

| Transport | Test | Result |
|---|---|---|
| BLE | `wodle_ble.py probe` (scan + auto-connect close peers) | connected to 4 real devices; **wodle absent → not advertising** |
| Classic | `system_profiler SPBluetoothDataType` | only Macs/keyboard/AirPods; **wodle not paired/known** |
| Classic | IOBluetooth active inquiry (12 s) | **0 discoverable → wodle not discoverable** |

The CLI itself **works** (it scanned, connected to, and dumped full GATT — incl. writable chars — from
4 real BLE devices). It just can't conjure the wodle into a connectable radio state.

**Only residual experiment:** free the wodle from the phone (phone BT off, or trigger its
"add device"/pairing flow), then `uv run tools/wodle_ble.py info <addr>` from a real Terminal (not
zellij/SSH — BT is silently denied there). Expectation: only standard services → confirms the dead end.

## What to use instead

- **finsh over UART (PA18/PA19)** — full local shell; 2 wires. → [`finsh_bringup.md`](finsh_bringup.md)
- **USB-CDC "HVR1" recovery** — enter from the device Settings menu → appears as `/dev/cu.usbmodem*`,
  a documented flash protocol, no wires/pairing. The Mac-accessible channel BLE never was. →
  [`firmware254.md`](firmware254.md) §2c, driven by `tools/wodle_flash.py`.
- **Self-hosted XiaoZhi WebSocket + OTA-host redirect** — the genuine "like the app" control plane
  (MCP tool calls over the cloud link); needs a self-hosted server (user previously declined).

## Tooling

`tools/wodle_ble.py` — bleak CLI: `scan` / `info <dev>` / `listen <dev> [char]` / `send <dev> <char>`.
`tools/wodle_spp.py` — classic-BT SPP probe (PyObjC, untested).
