# Talking to the wodle over Bluetooth — feasibility

Goal: a Mac CLI that communicates with the device like the official app, with no wires.

## VERDICT (2026-06-01)

**The CLI is written and works** (`tools/wodle_ble.py` — proven: it scanned, connected to, and
dumped full GATT from 4 real BLE devices incl. writable characteristics). **Whether it can drive the
wodle depends on one physical prerequisite + one open question:**

- **Prerequisite (same as the official app):** the device must be **advertising/pairable**. An
  exhaustive close-range probe did NOT see the wodle as a connectable BLE peripheral → it's bonded/
  connected to the phone and not advertising. **Free it from the phone (turn off phone BT, or trigger
  its "add device"/pairing flow), then run `wodle_ble.py probe`.**
- **Open question (settles in one command once it's pairable):** does its GATT server expose a
  **custom writable+notify service** (a control channel) or only standard services (GAP/GATT/DIS/
  Battery/HID)? Firmware evidence (below) leans toward standard-only + classic+cloud, but only a live
  `probe`/`info` dump proves it.

### Why "like the app, fully" is still limited (the cloud/classic part)

Even with a BLE channel, the app's *core* function — sharing the phone's internet via **BT-PAN** so
the device reaches the **XiaoZhi cloud** — can't be replicated by a macOS CLI:

1. **The app's link is BT-Classic + cloud, not an app↔device command protocol.** The phone pairs
   for audio (A2DP/AVRCP/HFP — note the iPhone `+IPHONEACCEV`/`+BIND` HFP commands) and **shares its
   internet to the device over BT-PAN**. The device then does everything against the **XiaoZhi
   cloud WebSocket**. "Communicating like the app" = *being the phone's internet + the cloud*, not
   sending the device commands.
2. **macOS can't script the roles the app fills.** There is no public/scriptable macOS API to act as
   a **BT-PAN NAP** or **HFP-AG** for an arbitrary third-party device. (Internet Sharing over BT PAN
   in System Settings is GUI-only and won't target this device usefully.)
3. **No custom BLE control service is evidenced.** The only custom 128-bit UUID is a **WebSocket
   token**; `sible`/"Sifli BLE command" is a **BLE RF-test** command (`enter_dut`/`tx_test`); the
   `sibles` stack is used for **pairing + GATT *client* role + RF test**. The GATT DB is built
   **dynamically** (no static attribute table in the image — static parse was pure noise), and the
   device's own server appears to expose only standard services.
4. **The device isn't reachable over BLE.** It doesn't advertise (bonded/auto-connected to the
   phone), and its BLE usage is largely **central/client**, not a connectable command peripheral.

**Residual BLE hope (small, hardware-gated):** static analysis can't 100% disprove a hidden writable
service. The ONLY way to settle it is a live GATT dump while the device advertises — see the
experiment below. Expectation: only standard services (GAP/GATT/DIS/Battery/HID) → confirms the
dead end. `tools/wodle_ble.py` is built and ready for exactly this if you can get it pairable.

**What to use instead:** UART finsh (2 wires, most capable) or the self-hosted XiaoZhi WebSocket
(the genuine "like the app" control plane). See below.

## What the firmware says about the app↔device link

- **BT-Classic is the primary link**: A2DP + AVRCP (audio), **HFP with `+IPHONEACCEV` / `+BIND`**
  (iPhone-specific hands-free accessory commands), and **PAN/BNEP** (the phone shares internet to
  the device). `boot.network_mode=bt`, `auto_connect=1`. **[C-RE]**
- **BLE is used for pairing/bonding + as a GATT *client*** (`BLE_GAP_BOND_*`, `sifli_add_remote_char`,
  `GATTC_*`). The `sible` / "Sifli BLE command" finsh command is a **BLE RF-test tool**
  (`enter_dut`/`tx_test`/`rx_test`), **not** an app control API. **[C-RE]**
- The one custom UUID `258EAFA5-E914-47DA-95CA-C5AB0DC85B11` is a **WebSocket token/key** (found in
  `wsock_connect`/`HANDSHAKE` code), **not a GATT service**. **[C-RE]**
- The real AI/control plane is the **cloud WebSocket** (`wss://api.tenclass.net/xiaozhi/v1/`,
  XiaoZhi protocol: hello/listen/tts/stt/iot/mcp/abort), reached over the PAN internet. **[C-RE]**

## Implication

The app most likely (a) pairs over **BT-Classic** for audio, (b) **shares the phone's internet via
BT-PAN**, and (c) the device then does everything else against the **cloud**. There is **no
discovered local BLE "send a command" service** the app uses.

A Mac **BLE** CLI is therefore only viable IF the device exposes a writable/notify GATT service we
haven't seen (strings can't prove a negative — needs live GATT discovery). A Mac replicating the
**BT-Classic PAN-NAP / HFP-AG** role is **not feasible** via a simple CLI (macOS doesn't expose those
roles to scripting). The **cloud WebSocket** is the true "like the app" control path but needs a
self-hosted XiaoZhi server (user declined).

## Reachability — exhaustively tested (2026-06-01), device NOT connectable in current state

| Transport | Test | Result |
|---|---|---|
| BLE | `wodle_ble.py probe` (scan + auto-connect close peers) | connected to 4 real devices (Qingping, Bose, …); **wodle absent → not advertising** |
| Classic | `system_profiler SPBluetoothDataType` (full paired/known list) | only Macs/keyboard/AirPods; **no "AI Dou" → not paired/known to this Mac** |
| Classic | IOBluetooth active inquiry (12 s) | **0 discoverable devices → wodle not discoverable** |

**Conclusion:** the wodle is bonded/attached to the phone and is neither advertising (BLE) nor
discoverable (classic). **No software — including the official app — can connect to a Bluetooth
device that isn't advertising/discoverable.** The official app could only connect after a one-time
**pairing** step. So the single unavoidable prerequisite is to put the wodle in pairing/advertising
mode (turn off phone BT and/or trigger its "add device" flow), then run `wodle_ble.py probe`.

The CLI itself is **written and proven functional** (it connected to + dumped GATT from 4 BLE
devices). What it cannot do is conjure a device into a connectable radio state — that's physical.

**Update:** firmware confirms the device **registers its own GATT server** (`appm_add_svc`,
`attmdb_att_get_permission`, `svc->svc.att_db`) and is BLE dual-role (also a HID host:
`gapm_scan_start`/`hidh_connect_req`). So a connectable BLE GATT server almost certainly EXISTS →
the `wodle_ble.py` CLI approach is **viable**; we just can't enumerate the services until the device
advertises (it won't while bonded to the phone). Net: "can we write the CLI" = **yes, viable**;
"prove it on the wodle" = one physical step (free it from the phone) away.

## Live scan result (2026-06-01)

`tools/wodle_ble.py scan` from the Mac saw ~25 BLE devices (BT works here) but **NO "AI Dou"/wodle**
— consistent with the device being bonded/connected to the user's phone (not advertising), or using
BT-Classic for the app link.

## The one cheap experiment that could still open a BLE path

1. **Make the device advertise/pairable**: power it with the phone OUT of range, or trigger its
   "add device"/pairing flow (whatever the official app does on first setup). Optionally unpair it
   from the phone.
2. From a normal Ghostty/Terminal (NOT zellij/SSH — BT is silently denied there):
   ```
   uv run --with bleak python tools/wodle_ble.py scan          # find "AI Dou"
   uv run --with bleak python tools/wodle_ble.py info <addr>    # dump every GATT service/char
   ```
3. If `info` shows a service with **write + notify** characteristics → there may be a usable channel;
   `tools/wodle_ble.py listen/send` can then probe it.
   If it only shows standard services (GAP/GATT/DIS/DFU) → no app control over BLE; the app uses
   BT-Classic + cloud, and a BLE CLI is a dead end.

## Realistic alternatives to "talk to the device"

- **finsh over UART (PA18/PA19)** — full local control/inspection; needs 2 wires. (`finsh_bringup.md`)
- **Self-hosted XiaoZhi WebSocket + OTA-host redirect** — the genuine "like the app" control plane
  (MCP tool calls), no wires; user previously declined. Same stack as the picture-book project.

## Tooling

`tools/wodle_ble.py` — bleak-based CLI: `scan` / `info <dev>` / `listen <dev> [char]` /
`send <dev> <char> <hex|str:text>`. Ready for the experiment above.
