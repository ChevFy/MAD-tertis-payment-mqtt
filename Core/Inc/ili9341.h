/**
 * ili9341.h - minimal ILI9341 SPI TFT driver for STM32L432 (SPI3)
 *
 * Landscape 320x240. Uses CubeMX-generated hspi3 + pin labels from main.h
 * (LCD_CS / LCD_DC / LCD_RST / LCD_BL). Fallback pin defines below let the
 * file compile even if CubeMX code has not been regenerated yet, but hspi3
 * still requires regeneration (Project -> Generate Code).
 */
#ifndef ILI9341_H
#define ILI9341_H

#include "main.h"
#include <stdint.h>

/* --- Fallback pin mapping (matches payment_mqtt.ioc) --- */
#ifndef LCD_CS_Pin
#define LCD_CS_Pin        GPIO_PIN_9
#define LCD_CS_GPIO_Port  GPIOA
#endif
#ifndef LCD_DC_Pin
#define LCD_DC_Pin        GPIO_PIN_10
#define LCD_DC_GPIO_Port  GPIOA
#endif
#ifndef LCD_RST_Pin
#define LCD_RST_Pin       GPIO_PIN_11
#define LCD_RST_GPIO_Port GPIOA
#endif
#ifndef LCD_BL_Pin
#define LCD_BL_Pin        GPIO_PIN_12
#define LCD_BL_GPIO_Port  GPIOA
#endif

/* Screen size (landscape) */
#define LCD_W 320
#define LCD_H 240

/* RGB565 colors */
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_MAGENTA 0xF81F
#define C_GRAY    0x8410
#define C_DKBLUE  0x0010
#define C_ORANGE  0xFD20

void LCD_Init(void);
void LCD_Backlight(uint8_t on);
void LCD_FillScreen(uint16_t color);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void LCD_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t color, uint16_t bg, uint8_t size);
/* Draw text horizontally centered on the screen at row y. */
void LCD_DrawStringCentered(uint16_t y, const char *s, uint16_t color, uint16_t bg, uint8_t size);

#endif /* ILI9341_H */
