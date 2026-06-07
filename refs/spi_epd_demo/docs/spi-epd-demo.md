# SPI EPD Demo

本文解释 `firmware/spi_epd_demo` 展示的屏幕驱动方式。它面向还不知道这块屏如何用 SPI 驱动的开发者。

## 结论

Wodle 当前推荐的屏幕驱动路径是：

```text
application
  -> wodle_display / wodle_gray4_display
  -> wodle_epd
  -> LCDC1 SPI DCX
  -> UC8179C / UC8279 compatible EPD controller
  -> 528 x 792 EPD panel
```

不要把 `firmware/flash_smoke` 里的 GPIO bit-bang SPI 当作正式驱动参考。它只用于早期验证 app 能启动并点亮屏幕。

## 工程独立性

`firmware/spi_epd_demo` 是独立固件目标，可以单独构建为一个 app-only 升级包。但它不是单目录自包含工程。

源码构建依赖：

```text
board/wodle/
firmware/spi_epd_demo/
firmware/common/
scripts/build_app_package.sh
tools/mk_update.py
SiFli-SDK
```

对外分发有三种方式：

1. 只让对方烧录验证：发送 `firmware/spi_epd_demo/dist/update.json` 和 `firmware/spi_epd_demo/dist/hcpu_app.bin`。
2. 让对方从源码构建：发送上面的最小源码 bundle，不需要带 `project/build_wodle_hcpu/`、`.sconsign.dblite` 等构建产物。
3. 让对方继续开发：发送完整 `wodle-fw` 仓库，并告知本 demo 入口是 `firmware/spi_epd_demo`。

## 引脚

```text
RST        PA00  GPIO output
BUSY       PA02  GPIO input, low means busy
CS         PA03  LCDC1_SPI_CS
CLK        PA04  LCDC1_SPI_CLK
MOSI       PA05  LCDC1_SPI_DIO0
DCX        PA06  LCDC1_SPI_DIO1
```

`RST` 和 `BUSY` 仍由 GPIO 控制和读取。命令/数据传输走 LCDC1 的 SPI DCX 模式。

## SPI DCX 模型

`wodle_epd` 内部使用两类写入：

- 命令寄存器写入：用于发送 EPD 命令，例如 `0x12`。
- 数据写入：用于发送 framebuffer、LUT 或命令参数。

开发者通常不需要直接调用 HAL。应用层只需要准备 framebuffer，然后调用 `wodle_epd` 或显示层 API。

## Framebuffer

黑白 framebuffer：

```text
width        528
height       792
row bytes    99
frame bytes  52272
pixel        1 = white, 0 = black
```

内部排布按控制器扫描方式处理，应用层建议使用 `wodle_display` 绘图，避免自己计算偏移。

4 灰阶 framebuffer：

```text
plane10 bytes  52272
plane13 bytes  52272
total bytes   104544
```

灰阶语义：

```text
00 = black
01 = dark gray
10 = light gray
11 = white
```

## 刷新序列

GC 黑白全刷的核心流程：

```text
load GC LUT
write 0x13 new frame
write 0x12 display refresh
wait BUSY release
optional settle delay
write 0x10 old frame
```

DU 黑白快刷：

```text
load DU LUT
write 0x13 new frame
write 0x12 display refresh
wait BUSY release
write 0x10 old frame
```

DU 黑白局刷：

```text
load DU LUT
enter partial mode
set partial window
write partial 0x13 frame data
write 0x12 display refresh
wait BUSY release
sync partial 0x10 old frame
exit partial mode
```

4 灰阶全刷：

```text
write 0x10 plane10
write 0x13 plane13
load LUT_Grey
write 0x12 display refresh
wait BUSY release
```

## Demo 命令和 API 对照

| 命令 | API | 用途 |
| --- | --- | --- |
| `gc` / `text` / `white` / `black` / `checker` | `wodle_epd_refresh_full()` | 干净全屏刷新 |
| `du` | `wodle_epd_refresh_fast()` | 黑白全屏快刷 |
| `partial` | `wodle_epd_refresh_partial_fast()` | 黑白局部快刷 |
| `gray4` | `wodle_epd_refresh_gray4_full()` | 4 灰阶全屏刷新 |

本 demo 不覆盖灰阶局部刷新。

## 构建

环境要求：

```text
SiFli SDK v2.5.0 or compatible
SDK export.sh
arm-none-eabi toolchain from SDK
scons from SDK
python3
uv
board/wodle from this repo
```

从仓库根目录执行：

```sh
scripts/build_app_package.sh --firmware spi_epd_demo --version V1.4.0.9030 --at 2026-06-07T00:00:00Z
```

如果 SDK 不在默认路径，显式指定：

```sh
scripts/build_app_package.sh \
  --sdk /path/to/SiFli-SDK \
  --firmware spi_epd_demo \
  --version V1.4.0.9030 \
  --at 2026-06-07T00:00:00Z
```

脚本做的事情：

```text
link board/wodle -> $SDK/customer/boards/wodle
source $SDK/export.sh
cd firmware/spi_epd_demo/project
scons --board=wodle -j8
check first LOAD address == 0x12218000
copy output/main.bin -> dist/hcpu_app.bin
generate dist/update.json
```

生成：

```text
firmware/spi_epd_demo/dist/hcpu_app.bin
firmware/spi_epd_demo/dist/update.json
```

## 实机验证建议

烧录后连接 USB CDC，依次执行：

```text
history
status
gc
du
partial
partial
gray4
status
```

验收点：

- 启动日志显示 `bus=lcdc1_spi_dcx`。
- 每次刷新 `ret=0`。
- `timeout=0`。
- `gc` 能恢复干净画面。
- `du` 明显比 GC 默认总耗时短。
- `partial` 只更新目标区域。
- `gray4` 能看到黑、深灰、浅灰、白四个层级。
