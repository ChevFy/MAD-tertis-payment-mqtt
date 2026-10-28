# Hardware Wiring — payment_mqtt

MCU: **STM32L432KC** (UFQFPN32, Cortex-M4F @ 80 MHz). Logic level **3.3 V**.

All pin assignments below match `payment_mqtt.ioc`. After editing the `.ioc`,
open it in STM32CubeIDE and **regenerate code** (Project → Generate Code) so
`gpio.c`, `spi.c`, `main.h`, and the interrupt handlers get the new config.
Hand-written code stays inside the `/* USER CODE BEGIN/END */` markers.

---

## Full pin map

| STM32 pin | Signal       | Peripheral / Dir | Connects to          |
|-----------|--------------|------------------|----------------------|
| PA1       | SPI1_SCK     | SPI1 (AF5)       | RC522 SCK            |
| PA6       | SPI1_MISO    | SPI1 (AF5)       | RC522 MISO           |
| PA7       | SPI1_MOSI    | SPI1 (AF5)       | RC522 MOSI           |
| PA4       | GPIO out     | RC522 CS (NSS)   | RC522 SDA/SS         |
| PB0       | GPIO out     | RC522 RST        | RC522 RST            |
| PA2       | USART2_TX    | USART2           | ESP32 RX             |
| PA3       | USART2_RX    | USART2           | ESP32 TX             |
| PA8       | GPIO out     | spare            | (unused)             |
| PB1       | GPIO out     | spare            | (unused)             |
| PA13      | SWDIO        | SWD debug        | ST-Link              |
| PA14      | SWCLK        | SWD debug        | ST-Link              |
| **PA0**   | GPIO EXTI0   | **BTN1** in      | Button 1 → GND       |
| **PA5**   | GPIO EXTI5   | **BTN2** in      | Button 2 → GND       |
| **PB3**   | SPI3_SCK     | SPI3 (AF6)       | LCD SCK/CLK          |
| **PB5**   | SPI3_MOSI    | SPI3 (AF6)       | LCD SDA/MOSI/DIN     |
| **PB4**   | SPI3_MISO    | SPI3 (AF6)       | LCD — reserved, N/C  |
| **PA9**   | GPIO out     | **LCD_CS**       | LCD CS               |
| **PA10**  | GPIO out     | **LCD_DC**       | LCD DC/RS            |
| **PA11**  | GPIO out     | **LCD_RST**      | LCD RESET            |
| **PA12**  | GPIO out     | **LCD_BL**       | LCD LED/BL           |

Bold = added in this revision.

---

## 1. RC522 RFID reader (SPI1) — existing

| RC522 | STM32 | Note                         |
|-------|-------|------------------------------|
| SDA   | PA4   | CS, active low (soft NSS)    |
| SCK   | PA1   | SPI1 clock                   |
| MOSI  | PA7   | SPI1 MOSI                    |
| MISO  | PA6   | SPI1 MISO                    |
| RST   | PB0   | reset, high = run            |
| IRQ   | —     | not used                     |
| GND   | GND   |                              |
| 3.3V  | 3V3   | **3.3 V only — not 5 V**     |

SPI1: Mode 0, MSB first, ~2.5 MBit/s. Verify at boot by reading `VersionReg`
(`0x37`) → expect `0x91`/`0x92`.

## 2. ESP32 link (USART2) — existing

| STM32      | ESP32      |
|------------|------------|
| PA2 (TX)   | RX         |
| PA3 (RX)   | TX         |
| GND        | GND        |

Cross TX↔RX. Common ground required. 3.3 V logic both sides. 115200 8N1.
STM32 sends `CMD <action> <uid> [amount]\r\n`; ESP32 replies `RESP <topic> OK|ERR [extra]`, terminated by `\r`/`\n`. Full line protocol in `topup-station.md`.

## 3. Buttons ×2 (EXTI) — new

Both wired **pin → button → GND**. Internal **pull-up** enabled, so idle =
HIGH, pressed = LOW. Trigger on **falling edge** (press).

```
   3V3
    |  (internal pull-up)
    |
  PA0 -----[ BTN1 ]----- GND
  PA5 -----[ BTN2 ]----- GND
```

| Button | STM32 | EXTI line | IRQ handler       |
|--------|-------|-----------|-------------------|
| BTN1   | PA0   | EXTI0     | `EXTI0_IRQHandler`    |
| BTN2   | PA5   | EXTI5     | `EXTI9_5_IRQHandler`  |

No external resistor needed. Add ~100 nF across each button to GND if debounce
in hardware is wanted; otherwise debounce in software (ignore re-trigger within
~50 ms). Handle presses in `HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)`:

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) { /* BTN1 */ }
    if (GPIO_Pin == GPIO_PIN_5) { /* BTN2 */ }
}
```

## 4. LCD-TFT (SPI3) — new

SPI display (ILI9341 / ST7735 class) on its **own bus (SPI3)** — independent of
RC522 on SPI1, so no bus contention. MISO not used (write-only); PB4 reserved
but left **not connected**.

| LCD pin     | STM32 | Note                                   |
|-------------|-------|----------------------------------------|
| SCK / CLK   | PB3   | SPI3_SCK                               |
| SDA / MOSI  | PB5   | SPI3_MOSI                              |
| CS          | PA9   | chip select, active low                |
| DC / RS     | PA10  | data/command select                    |
| RST / RESET | PA11  | hardware reset, active low             |
| LED / BL    | **3V3** | backlight — tie straight to 3V3 (PA12 then unused) |
| VCC         | **5V**  | see warning below                      |
| GND         | GND   |                                        |

SPI3: Mode 0, MSB first, ~10 MBit/s.

Actual module in use: **red 2.8" ILI9341 240x320 "v1.2"** (TJCTM24028 class).

> **Power — the "no backlight" trap:** this red board has an on-board
> **AMS1117-3.3 regulator** (~1.1 V dropout). Feeding `VCC = 3.3 V` leaves only
> ~2.2 V after the regulator → display and backlight stay dark. **Feed `VCC = 5 V`**
> (Nucleo `5V`/VIN pin). Logic pins (SCK/MOSI/CS/DC/RST) stay at 3.3 V — the board
> has level shifters and is 5 V-tolerant. The `LED` pin must be connected (not
> floating); tie it to 3V3 for always-on backlight.
>
> Bring-up order: wire only `VCC=5V`, `LED=3V3`, `GND` first → backlight must
> light (white screen). Only then wire the logic pins and run the code.

---

## Power & ground

- Single 3.3 V rail for MCU, RC522, LCD logic.
- All grounds common (STM32, RC522, LCD, ESP32).
- LCD backlight can draw tens of mA — size the 3.3 V supply accordingly.

## Pins still free after this revision

PA15, PB6, PB7 (plus JTAG-remap PB4 used by SPI3_MISO reserve). PA8/PB1 remain
as spare GPIO outputs.
