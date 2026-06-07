# 小豆子 SF32LB52 固件 — 模块文档

## 目录

- [SD 卡（SPI1，寄存器级）](#sd-卡spi1寄存器级)
- [RTC BKP 亮度持久化](#rtc-bkp-亮度持久化)
- [休眠唤醒（PMU Hibernate）](#休眠唤醒pmu-hibernate)
- [背光 PWM（GPTIM1_CH4 / PA1）](#背光-pwmgptim1_ch4--pa1)
- [触摸手势](#触摸手势)
- [E-ink 屏幕（UC8179C 3.68" 528×792）](#e-ink-屏幕uc8179c-368-528792)
- [UI 架构](#ui-架构)
- [应用模式](#应用模式)
- [引脚分配表](#引脚分配表)
- [编译](#编译)

---

## SD 卡（GPIO 位脉冲模拟 SPI）

### 文件

`src/sd_spi.h` / `src/sd_spi.c`

### 原理

纯 GPIO 位脉冲（bit-bang）模拟 SPI 模式 0，不涉及 SPI1 外设。每个比特通过 `rt_pin_write` 直接操控 MOSI/CLK 引脚产生，MISO 用 `rt_pin_read` 采样。时序靠 `for` 循环延迟，无硬件 SPI 参与。

```c
static uint8_t spi_byte(uint8_t b)
{
    uint8_t r = 0;
    for (int i = 7; i >= 0; i--) {
        sd_mosi((b >> i) & 1);
        sd_clk(PIN_HIGH);
        r = (r << 1) | sd_miso();
        sd_clk(PIN_LOW);
    }
    return r;
}
```

### CS 控制

GPIO 直接拉高/拉低，无 HOLD_FRAME 机制。

### 初始化流程

CMD0 → CMD8 → ACMD41 → CMD58 → CMD16 标准 SD 流程，全部通过 `sd_cmd_raw` 逐字节脉冲完成。

### 引脚

| 信号 | 引脚 | GPIO 功能 |
|------|------|-----------|
| MOSI | PA24 | `GPIO_A24` 推挽输出 |
| MISO | PA25 | `GPIO_A25` 上拉输入 |
| CLK  | PA28 | `GPIO_A28` 推挽输出 |
| CS   | PA29 | `GPIO_A29` 推挽输出 |

---

## RTC BKP 亮度持久化

### 文件

`src/rtc_config.h` / `src/rtc_config.c`

### 原理

利用 RTC 备份寄存器 `BKP1R` 存储亮度档位。hibernate 模式下 `VDD_RTC` 由 PMU 内部 LDO 供电，BKP 寄存器不丢失。

**注意**：`BKP0R` 被 SDK `HAL_PMU_EnterHibernate()` 内部占用（写入 `0x5050` 作诊断标记），所以用 `BKP1R`（idx=1）。

### API

```c
void rtc_config_init(void);           // HAL_RTC_Init(RTC_INIT_SKIP) + CR |= RTC_CR_BKP
void rtc_config_save(uint8_t val);    // 写 BKP1R: val | 0xCF000000
uint8_t rtc_config_load(void);        // 读 BKP1R，魔数校验，返回 val 或 0xFF
```

### 存储格式

```
BKP1R[31:24] = 0xCF (魔数)
BKP1R[7:0]   = brightness_level
```

---

## 休眠唤醒（PMU Hibernate）

### 文件

`src/power.h` / `src/power.c`

### 休眠流程

```c
void power_sleep(void)
```

1. `backlight_set(0)` 关背光
2. PA34 (PWRKEY) 设为唤醒引脚：`HAL_PMU_SelectWakeupPin` + `HAL_PMU_EnablePinWakeup`
3. PA24–PA44 全部拉低（`HAL_PIN_Set` 为 GPIO 下拉）
4. 关 LDO18 / VDD33_LDO2 / VDD33_LDO3
5. `__disable_irq()` + `HAL_PMU_EnterHibernate()`

### 唤醒判断

```c
int power_wakeup_reason(void);  // 1 = hibernate 唤醒, 0 = 冷启动
```

检测 `SystemPowerOnModeGet() == PM_HIBERNATE_BOOT`。

### 前置准备

休眠前保存亮度到 RTC BKP，显示 SLEEP 全黑屏后再进休眠。

```c
rtc_config_save(brightness_level);
rt_mutex_take(&epd_lock, RT_WAITING_FOREVER);
epd_fill(fb, 0);
epd_render_partial_start(fb);
while (!epd_render_partial_poll()) rt_thread_mdelay(10);
rt_mutex_release(&epd_lock);
power_sleep();
```

---

## 背光 PWM（GPTIM1_CH4 / PA1）

### 文件

`src/backlight.h` / `src/backlight.c`

### 配置

| 参数 | 值 |
|------|-----|
| 定时器 | GPTIM1 |
| 通道 | CH4 (CCR4) |
| 引脚 | PA1 (`GPTIM1_CH4`) |
| 预分频 | 48-1 (48MHz → 1MHz) |
| 周期 | 1000-1 (1MHz → 1KHz) |
| 模式 | PWM 1 (CCMR2.OC4M=6) |

```c
void backlight_init(void);           // 开启时钟 + 配置 GPTIM1 + 设 pinmux
void backlight_set(int percent);     // 0-100，0 关 CCER 输出
int  backlight_get(void);
```

### 亮度档

有 10 档亮度：`{0, 30, 35, 40, 45, 50, 55, 60, 65, 70}`。

KEY3 (PA43) 减档，KEY2 (PA44) 加档，只允许按键调节。

---

## 触摸手势

### 区域判定

| 手势 | 条件 |
|------|------|
| HOME | 起始 y > 80% 区域高度，且向上滑动 > 阈值 |
| CTRL | 起始 y < 20% 区域高度，且向下滑动 > 阈值 |
| L    | 水平左滑 > 阈值 |
| R    | 水平右滑 > 阈值 |
| TAP  | 起点终点距离 < 5% 区域尺寸 |

### 动态坐标追踪

初始范围 `min_x=20, max_x=526, min_y=5, max_y=791`，每次触摸更新边界。

---

## E-ink 屏幕（UC8179C 3.68" 528×792）

### 文件

`src/epd_uc8179c.h` / `src/epd_uc8179c.c`

### 接口

```c
void epd_hw_init(void);                           // 全屏初始化 + GC LUT 加载
void epd_render(const uint8_t *fb);               // 全屏刷新（DU LUT + 0x10 写回）
void epd_clear(void);                             // 全屏清白（GC LUT + 0x10 写回）
void epd_render_partial_start(const uint8_t *fb); // 启动 DU 模式局部刷新
int  epd_render_partial_poll(void);               // 查询刷完没，返回 1=完成
void epd_render_grayscale4(const uint8_t *bp0, const uint8_t *bp1); // 4 级灰度
void epd_fill(uint8_t *fb, int black);            // 0=白, 1=黑
```

### 刷新策略

- **B/W 显示**：DU 波形（0x50=0xD7），减少闪烁。每次刷新后写回 0x10 保证旧帧同步
- **清屏**：GC 波形（0x50=0x97），先写 0x13 再 0x12 刷新，完成后写回 0xFF 到 0x10
- **4 级灰度**：双 bit-plane（0x10 = bp1, 0x13 = bp0），专用灰度 LUT，不设 0x50

### 初始化序列（UC8179C 参考）

```
0x00 {0x3F, 0x4A}         面板设置
0x03 0x10                 电源模式
0x01 {0x03,0x00,0x78,0x78,0x17}  电源（VGH/VGL/VSH）
0x06 {0x25,0x25,0x3C}    升压软启动
0x82 0x24                 VCOM
0x30 0x0F                 帧频
0x61 {0x03,0x18,0x02,0x58} 分辨率 792×600
wait BUSY
0x65 {0x00,0x00,0x00,0x00} 闪存模式
0xE1 0x02                 Gate 扫描
0x10 填充 0xFF × 52272   旧帧全白（关电前）
0x04                      电源开启
wait BUSY
加载 GC LUT（0x50=0x97 + 0x20-0x24）
```

### 像素格式

- `0xFF` = 白底（默认）
- `0x00` = 黑

坐标：x 0–527（列），y 0–791（行）。帧缓冲布局：`fb[x * 99 + y/8]`，每列 99 字节，每字节 8 行垂直像素。

### SPI 配置

- LCDC1 SPI DCX 1-data 模式
- 时钟 12MHz
- Pixel Format: RGB565

---

## UI 架构

`main.c` 包含完整 UI 层：

### 网格主页

4×3 网格（528px 宽），带横竖分割线。顶栏显示时间+电量，底栏显示亮度+页码。左右滑动翻页，TAP 选中图标。

### 应用模式 (app_mode)

| 值 | 模式 | 文件 |
|----|------|------|
| 0 | 桌面网格 | main.c |
| 1 | SD 卡测试 | main.c |
| 2 | 电池详情（1bit 渲染+像素抖动图标） | main.c |
| 3 | 4 级灰度测试 | main.c |
| 4 | 温湿度显示 | main.c |

### 多线程安全

所有 EPD 硬件操作通过 `epd_lock` 互斥量保护，避免主循环与触屏线程竞争 LCDC 硬件。

### 主循环

```
main()
├── power_wakeup_reason() → cold/hibernate
├── rtc_config_init(), rtc_config_load()
├── backlight_init(), backlight_set()
├── 创建 key_thread（按键检测 + 亮度调节）
├── epd_hw_init() + u8g2_port_init() + draw_screen() + epd_render()
├── 创建 touch_thread（RT-Thread touch 设备事件）
├── 创建 battery_thread（BQ27220 + AW32001E 轮询）
└── while(1):
    ├── sleep_requested? → 保存亮度 → 显示 SLEEP → power_sleep()
    ├── need_refresh && app_mode==0 → draw_screen() → epd_render_partial_start()
    └── epd_render_partial_poll() → refr_busy 完成
```

### 网格应用跳转

TAP 处理链：
1. `app_mode != 0` → 退出当前模式回到桌面（app_mode=0, need_refresh=1）
2. `app_mode == 0` → grid_hit_test() 判断点击位置
3. 匹配标签名 → 设置 app_mode → 调用功能函数 → 返回后 need_refresh=0

---

## 引脚分配表

| 引脚 | 功能 | 用途 |
|------|------|------|
| PA0 | GPIO_A0 | E-INK_RST |
| PA1 | GPTIM1_CH4 | 背光 PWM |
| PA2 | GPIO_A2 | E-INK_TE/BUSY |
| PA3 | SPI2_CS / GPIO_A3 | E-INK_CS |
| PA4 | SPI2_CLK / GPIO_A4 | E-INK_CLK |
| PA5 | SPI2_DIO / GPIO_A5 | E-INK_MOSI |
| PA6 | SPI2_DI / GPIO_A6 | E-INK_DC |
| PA7 | I2C1_SCL | 触摸 SCL |
| PA8 | I2C1_SDA | 触摸 SDA |
| PA9 | GPIO_A9 | CTP_RESET |
| PA10 | GPIO_A10 | AUDIO_PA_CTRL / 电源使能 |
| PA11 | GPIO_A11 | KEY2 (减亮) |
| PA24 | GPIO_A24 | SD_MOSI（GPIO 位脉冲） |
| PA25 | GPIO_A25 | SD_MISO（GPIO 位脉冲） |
| PA28 | GPIO_A28 | SD_CLK（GPIO 位脉冲） |
| PA29 | GPIO_A29 | SD_CS（GPIO 位脉冲） |
| PA34 | GPIO_A34 | PWRKEY (唤醒) |
| PA42 | GPIO_A42 | CTP_INT |
| PA43 | GPIO_A43 | KEY3 (增亮) |
| PA44 | GPIO_A44 | VBUS_DET |

---

### 已知问题 / 设计决策

- **SD 卡用 GPIO 位脉冲**：SDIO 引脚与 MPI2（NOR Flash）冲突，SPI1 的 HAL 轮询会卡死，故用 GPIO 直接控制 PA24/25/28/29 模拟
- **RTC_INIT_SKIP**：跳过 RTC 时钟初始化，仅设置 state 和 BKP 位
- **无串口**：所有诊断通过 E-ink 屏幕显示，而非串口
- **0x61 分辨率 792×600**：物理面板 528×792，但 0x61 寄存器设 792×600 并写 52272 字节。此值来自 UC8179C 参考和 SDK 驱动
- **B/W 用 DU 波形**：UC8179C 参考对 B/W 显示使用 DU LUT（0x50=0xD7），GC 仅用于清屏
- **0x10 写回同步**：每次刷新后将帧数据写回 0x10 寄存器，保证控制器旧帧缓存同步

---

## 编译

```bash
export SIFLI_SDK=~/.sifli/sdk/SiFli-SDK/v2.4.6
export RTT_CC=gcc
export RTT_EXEC_PATH=~/.sifli/tools/arm-none-eabi-gcc/14.2.1/bin
export PATH="$RTT_EXEC_PATH:$PATH"
export PYTHONPATH="$SIFLI_SDK/tools/build:$PYTHONPATH"
export FW_VERSION="V1.0.0"
cd project
scons --board=sf32lb52-lcd_n16r8 -j8
```

输出：`build_sf32lb52-lcd_n16r8_hcpu/output/tfupdate/firmware/`
