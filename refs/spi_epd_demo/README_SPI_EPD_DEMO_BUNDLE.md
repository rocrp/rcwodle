# SPI EPD Demo 最小源码包

这个压缩包包含构建 Wodle SPI EPD 示例固件所需的最小源码，同时包含一次已构建好的刷机产物，方便直接用 HVR Recovery 验证屏幕。

## 这个 Demo 演示什么

- 屏幕正式传输路径使用 `LCDC1 SPI DCX`。
- 屏幕控制器按 UC8179C / UC8279 兼容命令和 LUT 驱动。
- 应用代码调用 `wodle_epd`、`wodle_display`、`wodle_gray4_display`。
- 不使用早期 bring-up 的 GPIO bit-bang SPI 作为正式路径。
- 不覆盖灰阶局部刷新。

## 包内内容

```text
README_SPI_EPD_DEMO_BUNDLE.md  本说明
board/wodle/                    Wodle 板卡定义，供 SiFli SDK 使用
firmware/common/                公共显示、EPD、USB 日志、前光模块
firmware/spi_epd_demo/          SPI EPD 示例固件目标
scripts/build_app_package.sh    构建和 app-only 打包脚本
tools/mk_update.py              update.json 生成工具
docs/spi-epd-demo.md            屏幕驱动说明和测试步骤
```

包内同时包含已构建好的刷机产物：

```text
firmware/spi_epd_demo/dist/hcpu_app.bin
firmware/spi_epd_demo/dist/update.json
```

以下构建中间产物不会放进包里：

```text
firmware/spi_epd_demo/project/build_wodle_hcpu/
firmware/spi_epd_demo/project/.sconsign.dblite
```

## 环境要求

- SiFli SDK v2.5.0 或兼容版本。
- SDK 目录下有可用的 `export.sh`。
- SDK 环境能提供 `arm-none-eabi` 工具链。
- SDK 环境能提供 `scons`。
- 本机有 `python3`。
- 本机有 `uv`，用于运行 `tools/mk_update.py`。

## 从源码构建

在压缩包解压后的根目录执行：

```sh
scripts/build_app_package.sh \
  --sdk /path/to/SiFli-SDK \
  --firmware spi_epd_demo \
  --version V1.4.0.9030 \
  --at 2026-06-07T00:00:00Z
```

脚本会自动完成：

1. 把 `board/wodle` 链接到 `$SDK/customer/boards/wodle`。
2. source SDK 的 `export.sh`。
3. 在 `firmware/spi_epd_demo/project` 下执行 `scons --board=wodle -j8`。
4. 检查 ELF 第一个 `LOAD` 地址是否为 `0x12218000`。
5. 生成只包含 `hcpu_app.bin` 的 `update.json`。

构建成功后会生成：

```text
firmware/spi_epd_demo/dist/hcpu_app.bin
firmware/spi_epd_demo/dist/update.json
```

## 直接刷机

如果只是验证屏幕，可以不重新构建，直接用包内已有产物：

```text
firmware/spi_epd_demo/dist/update.json
```

使用 HVR Recovery 加载这个 `update.json` 进行烧录。

当前包内产物是 app-only 升级包，只包含：

```text
hcpu_app.bin @ 0x12218000
```

## 串口验证

烧录后正常启动设备，连接设备自身 USB CDC 串口，依次输入：

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

重点观察：

- 启动日志显示 `bus=lcdc1_spi_dcx`。
- 每次刷新返回 `ret=0`。
- `timeout=0`。
- `gc` 能执行干净全屏刷新。
- `du` 能执行更快的全屏刷新。
- `partial` 只更新目标局部区域。
- `gray4` 能看到黑、深灰、浅灰、白四个层级。
