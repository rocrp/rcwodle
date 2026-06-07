# spi_epd_demo

这个固件是给开发者看的最小 SPI EPD 示例。它演示 Wodle 屏幕当前推荐的正式驱动方式：

- 底层总线固定使用 `LCDC1 SPI DCX`。
- 屏幕控制器按 UC8179C / UC8279 兼容命令和 LUT 驱动。
- 应用侧不直接操作 LCDC 寄存器，而是调用公共模块 `wodle_epd`。
- 不使用早期 bring-up 的 GPIO bit-bang SPI。
- 不覆盖灰阶局部刷新。

## 独立性和分发方式

`spi_epd_demo` 是一个独立固件目标，可以单独编译、打包和烧录。但它不是单目录自包含工程，源码构建时仍依赖仓库里的板卡定义、公共显示模块和打包工具。

如果只是让对方验证屏幕驱动效果，直接发送：

```text
firmware/spi_epd_demo/dist/update.json
firmware/spi_epd_demo/dist/hcpu_app.bin
```

如果要让对方从源码构建，最小源码 bundle 需要包含：

```text
board/wodle/
firmware/spi_epd_demo/
firmware/common/
scripts/build_app_package.sh
tools/mk_update.py
```

其中 `firmware/spi_epd_demo/project/build_wodle_hcpu/`、`firmware/spi_epd_demo/project/.sconsign.dblite` 和 `firmware/spi_epd_demo/dist/` 是构建产物，不需要放进源码 bundle。`dist/` 只在发送预编译烧录包时需要。

如果对方已经拿到完整 `wodle-fw` 仓库，则不需要额外整理依赖。

## 环境要求

- SiFli SDK，当前本机验证使用 `SiFli-SDK v2.5.0`。
- SDK 内可用的 `export.sh`。
- `arm-none-eabi` GCC/readelf 工具链，由 SiFli SDK 环境提供。
- `scons`，由 SiFli SDK 环境提供。
- `python3`。
- `uv`，用于运行 `tools/mk_update.py` 打包脚本。
- Wodle 板卡定义 `board/wodle`，构建脚本会临时链接到 `$SDK/customer/boards/wodle`。

本仓库默认 SDK 路径为：

```text
/Users/godzilla/Documents/Code/workspace/wodle-dev/repos/opensifli/SiFli-SDK
```

如果对方 SDK 路径不同，构建时通过 `--sdk` 指定。

## 构建

从 `wodle-fw` 仓库根目录执行：

```sh
scripts/build_app_package.sh --firmware spi_epd_demo --version V1.4.0.9030 --at 2026-06-07T00:00:00Z
```

指定 SDK 路径：

```sh
scripts/build_app_package.sh \
  --sdk /path/to/SiFli-SDK \
  --firmware spi_epd_demo \
  --version V1.4.0.9030 \
  --at 2026-06-07T00:00:00Z
```

输出：

- `firmware/spi_epd_demo/dist/hcpu_app.bin`
- `firmware/spi_epd_demo/dist/update.json`

脚本会完成：

1. 把 `board/wodle` 接入 SDK 的 `customer/boards/wodle`。
2. source SDK 的 `export.sh`。
3. 在 `firmware/spi_epd_demo/project` 下执行 `scons --board=wodle -j8`。
4. 检查 ELF 第一个 `LOAD` 地址是否为 `0x12218000`。
5. 生成只包含 `hcpu_app.bin` 的 `update.json`。

构建成功时会看到类似：

```text
ELF 第一个 LOAD 地址: 0x12218000
update.json OK: size=339104 crc=0x73869DA5
```

## 烧录和串口

1. 用 HVR Recovery 烧录 `firmware/spi_epd_demo/dist/update.json`。
2. 普通启动后连接设备自身 USB CDC 串口。
3. 输入 `history` 查看启动日志。
4. 输入 `help` 查看命令。

## USB CDC 命令

- `history`：回放启动日志，由公共 `usb_log` 模块处理。
- `status`：打印刷新次数、最后一次刷新结果、前光和 EPD 统计。
- `init`：重新初始化 EPD。
- `white`：绘制白屏并执行 GC 全刷。
- `black`：绘制黑屏并执行 GC 全刷。
- `checker`：绘制棋盘格并执行 GC 全刷。
- `text` / `gc`：绘制 GC 说明页并执行 GC 全刷。
- `du`：绘制 DU 说明页并执行 DU 全屏快刷。
- `partial`：更新一个局部区域并执行 DU 局部刷新。
- `gray4`：绘制 4 灰阶条带并执行 4 灰阶全屏刷新。
- `sleep`：让 EPD 进入 sleep。
- `light N`：设置前光亮度，`N=0..100`。
- `up` / `down`：调高或调低前光。

## 观察重点

每次刷新都会输出类似字段：

```text
stats tag=... busy=... timeout=... total=... lut=... new=... cmd_busy=... settle=... old=... bytes=... partial=... rect=... bus=lcdc1_spi_dcx
```

含义：

- `busy`：触发 `0x12` 刷新后等待 BUSY 引脚释放的时间。
- `total`：底层刷新函数总耗时。
- `lut`：写 LUT 的耗时，连续使用同一种 LUT 时可能为 `0`。
- `new`：写 `0x13` 新帧或低位平面的耗时。
- `old`：写 `0x10` old RAM 或高位平面的耗时。
- `settle`：驱动主动插入的固定等待，默认 GC 为 `3000 ms`。
- `bytes`：本次写入的新帧数据量。
- `partial` / `rect`：是否局刷，以及局刷区域。

## 示例覆盖的刷新 API

```c
wodle_epd_refresh_full(frame);
wodle_epd_refresh_fast(frame);
wodle_epd_refresh_partial_fast(frame, x, y, width, height);
wodle_epd_refresh_gray4_full(plane10, plane13);
```

灰阶局刷仍应按候选路径单独验证，不作为这份 SPI 入门 demo 的推荐用法。
