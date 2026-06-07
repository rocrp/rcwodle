# 公共固件模块

这里放后续冒烟测试共用的小模块。

## usb_log

状态：`已验证`

`usb_log` 基于 RT-Thread USB VCOM 设备 `vcom`，提供：

- `usb_log_init()`：打开设备自身 USB CDC 串口。
- `usb_log_write()` / `usb_log_printf()`：输出日志。
- 可选行回调：接收主机串口输入，用于后续测试命令。
- 历史日志缓存：保留最近 4 KB 文本日志，便于主机较晚打开串口后查看启动阶段输出。
- `history`：通过串口输入该命令，回放最近缓存日志。
- `clear-history`：通过串口输入该命令，清空缓存日志。

`history` 和 `clear-history` 是公共模块保留命令，不会传给应用层。普通输入会直接交给应用层回调。

`firmware/usb_cdc_smoke` 是已实机验证过的 USB CDC 基线，暂时保留原实现作为对照。新的硬件冒烟测试优先使用这里的公共模块。

## wodle_frontlight

状态：`已验证`

`wodle_frontlight` 封装设备前光控制：

- PA01
- `pwmt1`
- channel 4
- period `200000 ns`
- 用户可见亮度 `1..100` 映射到原始 duty `50..100`
- `0` 表示关闭

这个映射来自 `firmware/frontlight_smoke` 实机验证：原始 duty `0..40` 不出光，`50..100` 逐渐变亮。

## wodle_epd

状态：`已验证`

`wodle_epd` 是 UC8179C 黑白显示底层驱动：

- 有效显示 `528 x 792`
- 1bpp 帧大小 `52272 bytes`
- RST=PA0
- BUSY=PA2，低电平表示 busy
- LCDC1_SPI_CS=PA3
- LCDC1_SPI_CLK=PA4
- LCDC1_SPI_DIO0=PA5
- LCDC1_SPI_DIO1 / DCX=PA6

正式显示传输路径固定使用 LCDC1 SPI DCX。`wodle_epd` 不再暴露 GPIO bit-bang fallback，也不再提供上层绘图接口。

已验证能力：

- GC LUT 全屏刷新。
- DU LUT 全屏快刷。
- DU LUT 局部窗口刷新。
- 刷新耗时拆分统计：BUSY 等待、固定等待、新帧写入、old RAM 同步和总耗时。
- GC 全刷后的固定等待可配置，默认仍为 `3000 ms`。

当前实机结论：

- `settle 0` 下已通过 50 轮和 100 轮压力测试，均无 BUSY 超时和返回错误。
- 长时间快刷/局刷后存在轻微残留，后续刷新策略需要继续优化。

## wodle_display

状态：`已验证`

`wodle_display` 是应用侧正式显示层：

- 调用方提供 `52272 bytes` framebuffer。
- 提供白/黑清屏、点、线、矩形、填充矩形、棋盘格和 ASCII 文字绘制。
- 刷新时默认调用 `wodle_epd_refresh_full()`，底层走 LCDC1 SPI DCX。

`firmware/display_surface_smoke` 已通过实机验收：白屏、黑屏、边框、棋盘格、文字、图形原语和前光按键均正常。

## wodle_gray4_display

状态：`已验证`

`wodle_gray4_display` 是 4 灰阶 framebuffer 层：

- 调用方提供两个 `52272 bytes` 平面，分别对应 EPD 控制器 `0x10` 和 `0x13`。
- 颜色语义为黑、深灰、浅灰、白。
- 提供清屏、点、线、矩形、填充矩形和 RLE 载入。
- 刷新时调用 `wodle_epd_refresh_gray4_full()`。

`firmware/gray4_smoke` 已通过实机验收：4 灰阶条纹、棋盘、文字和抗锯齿文字页都能正常显示。

## wodle_font / wodle_text

状态：`已验证`

`wodle_font` 和 `wodle_text` 是正式文字渲染层第一版：

- 字体数据由 `tools/gen_text_font.py` 在构建期生成。
- 字形 coverage 以 `2bpp` 保存。
- 固件端完成 UTF-8 解码、glyph 查表、文本框换行、裁剪、左/中/右对齐。
- 输出到 `wodle_gray4_display`，用 4 灰阶像素软化文字边缘。
- `wodle_text_result_t` 会返回实际绘制边界，用于后续局刷 dirty rect。

`firmware/text_render_smoke` 已通过实机验收：4 灰阶运行时文字渲染效果明显改善，黑体显示效果当前最好。灰阶局部刷新已加入候选实现，仍需要通过 `partial-gray` 和 `stress N` 做实机验证后才能进入正式策略。
