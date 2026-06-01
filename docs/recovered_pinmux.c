/*
 * wodle / "AI Dou" — recovered pinmux for a custom SiFli SF32LB52x framework.
 *
 * Drop-in starting point for BSP_PIN_Init(). Pin functions + pulls were recovered
 * statically from the stock firmware (docs/firmware-analysis.md). Use the SiFli-SDK
 * HAL: HAL_PIN_Set(PAD_PAxx, <FUNC>, <PIN_NOPULL|PIN_PULLUP|PIN_PULLDOWN>, 1).
 *
 * Confidence: lines without a TODO are recovered with high confidence (peripheral
 * functions). TODO lines need the live finsh `pin` / `list_device` dump to confirm.
 *
 * Board: SF32LB52x N16R8, 528x792 portrait e-paper. Console UART1 on PA18/PA19.
 */
#include "bf0_hal.h"

void BSP_PIN_Init(void)
{
    /* ---- E-paper display: LCDC1 in dual-SPI ---- */
    HAL_PIN_Set(PAD_PA03, LCDC1_SPI_CS,   PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA04, LCDC1_SPI_CLK,  PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA05, LCDC1_SPI_DIO0, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA06, LCDC1_SPI_DIO1, PIN_NOPULL, 1);
    /* Frontlight: PA01 PWM (driver string: "force PA01 low") */
    HAL_PIN_Set(PAD_PA01, PA01_TIM,       PIN_NOPULL, 1);  /* GPTIM PWM; verify channel */

    /* ---- I2C1: CST816 touch @0x15 ---- */
    /* wodle runs LCD in 2-data-lane mode, freeing PA07/PA08 (DevKit's LCD DIO2/3) for I2C1 */
    HAL_PIN_Set(PAD_PA07, I2C1_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA08, I2C1_SDA, PIN_PULLUP, 1);  /* SDA inferred (adjacent + DevKit pattern) */

    /* ---- I2C2: AW32001 charger @0x49 + BQ27220 fuel gauge @0x55 ---- */
    HAL_PIN_Set(PAD_PA31, I2C2_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA32, I2C2_SDA, PIN_PULLUP, 1);

    /* ---- SPI1 = microSD/TF card (confirmed: matches DevKit "SPI1(TF card)" pins exactly) ---- */
    HAL_PIN_Set(PAD_PA28, SPI1_CLK, PIN_NOPULL,   1);
    HAL_PIN_Set(PAD_PA29, SPI1_CS,  PIN_NOPULL,   1);
    HAL_PIN_Set(PAD_PA24, SPI1_DIO, PIN_NOPULL,   1);
    HAL_PIN_Set(PAD_PA25, SPI1_DI,  PIN_PULLDOWN, 1);

    /* ---- Console / debug UART1 (PA18/PA19) ---- */
    HAL_PIN_Set(PAD_PA18, USART1_RXD, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA19, USART1_TXD, PIN_PULLUP, 1);

    /* ---- Buttons / control GPIOs (resolved via DevKit + board.conf cross-check) ---- */
    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);  /* KEY1 power (keep PD for UART-download) */
    HAL_PIN_Set(PAD_PA00, GPIO_A0,  PIN_PULLDOWN, 1);  /* display RESET (DevKit #LCD_RESETB) */
    HAL_PIN_Set(PAD_PA10, GPIO_A10, PIN_NOPULL,   1);  /* AW8155 speaker-amp enable */
    HAL_PIN_Set(PAD_PA44, GPIO_A44, PIN_PULLDOWN, 1);  /* VBUS / charger detect (input) */

    /*
     * ---- Still-unknown GPIOs: PA21, PA33, PA38, PA42, PA43 ----
     * Expected among these: EPD BUSY (input/IRQ), EPD D/C, touch INT (IRQ), touch RESET.
     * wodle REWIRED touch vs the DevKit (DevKit touch INT=PA31/RST=PA09, but wodle uses
     * PA31 for I2C2_SCL), so the DevKit labels do NOT apply here. Confirm via live `pin`:
     */
    /* HAL_PIN_Set(PAD_PA21, GPIO_A21, PIN_?, 1); */
    /* HAL_PIN_Set(PAD_PA33, GPIO_A33, PIN_?, 1); */
    /* HAL_PIN_Set(PAD_PA38, GPIO_A38, PIN_?, 1); */
    /* HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_?, 1); */
    /* HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_?, 1); */

    /*
     * ---- NOR flash on MPI2/QSPI2 (PA12,PA16,PA15,PA13,PA14,PA17) ----
     * Owned by the bootloader / ftab (XIP boot device @0x12000000). Do NOT re-mux
     * from the app. Listed here for reference only:
     *   PA12=MPI2_CS PA16=MPI2_CLK PA15=DIO0 PA13=DIO1 PA14=DIO2 PA17=DIO3
     */
}
