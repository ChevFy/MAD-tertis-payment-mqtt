/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "RC522.h"
#include "ili9341.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#ifndef BTN1_Pin
#define BTN1_Pin        GPIO_PIN_0
#define BTN1_GPIO_Port  GPIOA
#endif
#ifndef BTN2_Pin
#define BTN2_Pin        GPIO_PIN_5
#define BTN2_GPIO_Port  GPIOA
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t rx_byte;
char rx_buffer[100];
uint8_t rx_index = 0;
volatile uint8_t response_received = 0;

typedef enum { BTN_NONE = 0, BTN_NEXT, BTN_SELECT } btn_t;
volatile btn_t g_btn = BTN_NONE;
volatile uint32_t g_btn_tick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void App_Init(void);
void App_Task(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch) {
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_SPI3_Init();
  /* USER CODE BEGIN 2 */
  // 1. เริ่มต้นการทำงานของ RFID RC522
  /* USER CODE BEGIN 2 */
    MFRC522_Init();
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);

    printf("\r\n===================================\r\n");
    printf("   STM32 + RFID (CoolTerm Mode)\r\n");
    printf("===================================\r\n");

    // === เพิ่มส่วนเช็กการเชื่อมต่อ SPI ตรงนี้ ===
    uint8_t version = Read_MFRC522(0x37); // หรือ MFRC522_ReadRegister(VersionReg) ตามที่มีใน .h
    printf("[SPI TEST] RC522 Version Reg: 0x%02X\r\n", version);

    if (version == 0x91 || version == 0x92) {
      printf("[SPI TEST] RC522 Connected Successfully!\r\n");
    } else {
      printf("[SPI TEST ERROR] Cannot communicate with RC522 (Check Wiring/Pins!)\r\n");
    }

    printf("Waiting for RFID Card...\r\n\n");

    /* Buttons: force falling-edge EXTI + enable NVIC here, independent of the
       CubeMX GUI (which keeps resetting these to rising / NVIC-off on regen).
       PA0 -> EXTI0, PA5 -> EXTI9_5. */
    {
      GPIO_InitTypeDef bexti = {0};
      bexti.Pin  = BTN1_Pin | BTN2_Pin;
      bexti.Mode = GPIO_MODE_IT_FALLING;
      bexti.Pull = GPIO_PULLUP;
      HAL_GPIO_Init(GPIOA, &bexti);
      HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
      HAL_NVIC_EnableIRQ(EXTI0_IRQn);
      HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
      HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  App_Init();
  while (1)
  {
    App_Task();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    // เมื่อเจอขึ้นบรรทัดใหม่ (\n หรือ \r) แสดงว่าได้ข้อความตอบกลับครบถ้วน
    if (rx_byte == '\n' || rx_byte == '\r') {
      if (rx_index > 0) {
        rx_buffer[rx_index] = '\0';
        rx_index = 0;
        response_received = 1;
      }
    } else {
      if (rx_index < sizeof(rx_buffer) - 1) {
        rx_buffer[rx_index++] = rx_byte;
      }
    }

    // รับ Byte ถัดไป
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
  }
}

/* ===================== Top-up station app ===================== */

static btn_t get_btn(void) {
  btn_t b = g_btn;
  g_btn = BTN_NONE;
  return b;
}

/* Button interrupts: BTN1 = NEXT (scroll), BTN2 = SELECT (OK). */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  uint32_t now = HAL_GetTick();
  if (now - g_btn_tick < 200) return;            /* debounce */
  if (GPIO_Pin == BTN1_Pin)      { g_btn = BTN_NEXT;   g_btn_tick = now; }
  else if (GPIO_Pin == BTN2_Pin) { g_btn = BTN_SELECT; g_btn_tick = now; }
}

/* EXTI vectors live here (not in stm32l4xx_it.c) because the NVIC lines are not
   enabled in the .ioc. Do NOT enable EXTI line0 / line[9:5] in the CubeMX GUI,
   or the generated handlers in stm32l4xx_it.c will clash with these. */
void EXTI0_IRQHandler(void)   { HAL_GPIO_EXTI_IRQHandler(BTN1_Pin); }
void EXTI9_5_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(BTN2_Pin); }

static const char *HOME_OPTS[4] = { "ADD MONEY", "NEW GUEST", "REMOVE CARD", "CHECK BALANCE" };

/* Repaint one menu row in place (no screen clear) -> no flicker on scroll. */
static void draw_menu_row(const char **opts, int i, int sel) {
  uint16_t y = 60 + i * 48;
  uint16_t bg = (i == sel) ? C_ORANGE : C_GRAY;
  LCD_FillRect(30, y, 260, 38, bg);
  LCD_DrawString(45, y + 9, opts[i], C_BLACK, bg, 3);
}

/* Full draw: clears the screen once on entering a menu. */
static void draw_menu(const char *title, const char **opts, int n, int sel) {
  LCD_FillScreen(C_BLACK);
  LCD_FillRect(0, 0, LCD_W, 40, C_DKBLUE);
  LCD_DrawStringCentered(14, title, C_WHITE, C_DKBLUE, 2);
  for (int i = 0; i < n; i++) draw_menu_row(opts, i, sel);
}

/* Move highlight from oldSel to newSel by repainting only those two rows. */
static void menu_move(const char **opts, int oldSel, int newSel) {
  draw_menu_row(opts, oldSel, newSel);
  draw_menu_row(opts, newSel, newSel);
}

static void screen_msg(const char *title, const char *msg, uint16_t color) {
  LCD_FillScreen(C_BLACK);
  LCD_FillRect(0, 0, LCD_W, 40, C_DKBLUE);
  LCD_DrawStringCentered(14, title, C_WHITE, C_DKBLUE, 2);
  LCD_DrawStringCentered(110, msg, color, C_BLACK, 2);
}

static int menu_select(const char *title, const char **opts, int n, int startSel) {
  int sel = startSel;
  draw_menu(title, opts, n, sel);
  while (1) {
    btn_t b = get_btn();
    if (b == BTN_NEXT)        { int old = sel; sel = (sel + 1) % n; menu_move(opts, old, sel); }
    else if (b == BTN_SELECT) { return sel; }
    HAL_Delay(20);
  }
}

static uint8_t read_card(char *uid_out) {
  uint8_t type[2], uid[5];
  if (MFRC522_Request(0x52, type) != MI_OK) return 0;   /* 0x52 = PICC_REQALL */
  if (MFRC522_Anticoll(uid) != MI_OK) return 0;
  sprintf(uid_out, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
  MFRC522_Halt();
  return 1;
}

/* Show "tap card" with a live countdown. Returns 1 if a card was read. */
static int wait_card(const char *title, char *uid, uint32_t timeout_ms) {
  LCD_FillScreen(C_BLACK);
  LCD_FillRect(0, 0, LCD_W, 40, C_DKBLUE);
  LCD_DrawStringCentered(14, title, C_WHITE, C_DKBLUE, 2);
  LCD_DrawStringCentered(80, "TAP YOUR CARD", C_WHITE, C_BLACK, 2);
  uint32_t end = HAL_GetTick() + timeout_ms;
  int last = -1;
  while ((int32_t)(end - HAL_GetTick()) > 0) {
    int32_t remain = (int32_t)(end - HAL_GetTick());
    int sec = (remain + 999) / 1000;              /* ceil => 10..1 */
    if (sec != last) {
      char b[8];
      sprintf(b, "%ds", sec);
      LCD_FillRect(0, 150, LCD_W, 32, C_BLACK);   /* clear previous number */
      LCD_DrawStringCentered(150, b, C_YELLOW, C_BLACK, 4);
      last = sec;
    }
    if (read_card(uid)) return 1;
    HAL_Delay(50);
  }
  return 0;
}

static void send_cmd(const char *action, const char *uid, int amount) {
  char line[64];
  if (amount >= 0) sprintf(line, "CMD %s %s %d\r\n", action, uid, amount);
  else             sprintf(line, "CMD %s %s\r\n", action, uid);
  HAL_UART_Transmit(&huart2, (uint8_t *)line, strlen(line), HAL_MAX_DELAY);
}

/* Wait for "RESP <topic> OK|ERR [extra]". 1=OK, 0=ERR, -1=timeout. */
static int wait_resp(const char *topic, char *extra, uint32_t timeout_ms) {
  uint32_t end = HAL_GetTick() + timeout_ms;
  if (extra) extra[0] = '\0';
  while ((int32_t)(end - HAL_GetTick()) > 0) {
    if (response_received) {
      char line[100];
      strncpy(line, rx_buffer, sizeof(line) - 1);
      line[sizeof(line) - 1] = '\0';
      response_received = 0;
      if (strncmp(line, "RESP ", 5) == 0) {
        char *t = line + 5;
        char *sp = strchr(t, ' ');
        if (sp) {
          *sp = '\0';
          char *rest = sp + 1;
          if (strcmp(t, topic) == 0) {
            char *sp2 = strchr(rest, ' ');
            if (sp2) { *sp2 = '\0'; if (extra) { strncpy(extra, sp2 + 1, 31); extra[31] = '\0'; } }
            return (strncmp(rest, "OK", 2) == 0) ? 1 : 0;
          }
        }
      }
      /* non-matching line (debug / stale response): ignore */
    }
    HAL_Delay(5);
  }
  return -1;
}

/* ---- Mode 1: add money ---- */
static void mode_add_money(void) {
  char uid[16], extra[32];
  if (!wait_card("ADD MONEY", uid, 10000)) {
    screen_msg("ADD MONEY", "No card", C_YELLOW); HAL_Delay(1500); return;
  }
  screen_msg("ADD MONEY", "Checking...", C_WHITE);
  send_cmd("check_role", uid, -1);
  int r = wait_resp("/check_role", extra, 6000);
  if (r != 1) { screen_msg("ADD MONEY", r == 0 ? "Not in system" : "No response", C_RED); HAL_Delay(2000); return; }

  const char *opts[4] = { "20", "50", "100", "CANCEL" };
  int sel = menu_select("SELECT AMOUNT", opts, 4, 0);
  if (sel == 3) return;
  int amt = (sel == 0) ? 20 : (sel == 1) ? 50 : 100;

  screen_msg("ADD MONEY", "Processing...", C_WHITE);
  send_cmd("add_money", uid, amt);
  r = wait_resp("/add_money", extra, 8000);
  if (r == 1) { char m[24]; sprintf(m, "+%d DONE", amt); screen_msg("SUCCESS", m, C_GREEN); }
  else        { screen_msg("FAILED", r == 0 ? "Top-up failed" : "No response", C_RED); }
  HAL_Delay(2500);
}

/* ---- Mode 2: register guest ---- */
static void mode_create_guest(void) {
  char uid[16], extra[32];
  if (!wait_card("NEW GUEST", uid, 10000)) {
    screen_msg("NEW GUEST", "No card", C_YELLOW); HAL_Delay(1500); return;
  }
  screen_msg("NEW GUEST", "Checking...", C_WHITE);
  send_cmd("check_role", uid, -1);
  int r = wait_resp("/check_role", extra, 6000);
  if (r == 1) { screen_msg("NEW GUEST", "Already exists", C_YELLOW); HAL_Delay(2000); return; }
  if (r < 0)  { screen_msg("NEW GUEST", "No response", C_RED); HAL_Delay(2000); return; }

  /* r == 0 (invalid card) => not registered => ok to create */
  screen_msg("NEW GUEST", "Registering...", C_WHITE);
  send_cmd("create_guest", uid, -1);
  r = wait_resp("/create_guest", extra, 8000);
  if (r == 1) screen_msg("SUCCESS", "Guest created", C_GREEN);
  else        screen_msg("FAILED", r == 0 ? "Create failed" : "No response", C_RED);
  HAL_Delay(2500);
}

/* ---- Mode 3: remove member card ---- */
static void mode_remove_card(void) {
  char uid[16], extra[32];
  if (!wait_card("REMOVE CARD", uid, 10000)) {
    screen_msg("REMOVE CARD", "No card", C_YELLOW); HAL_Delay(1500); return;
  }
  screen_msg("REMOVE CARD", "Checking...", C_WHITE);
  send_cmd("check_role", uid, -1);
  int r = wait_resp("/check_role", extra, 6000);
  if (r < 0)  { screen_msg("REMOVE CARD", "No response", C_RED); HAL_Delay(2000); return; }
  if (r == 0) { screen_msg("REMOVE CARD", "Not in system", C_RED); HAL_Delay(2000); return; }
  if (strcmp(extra, "guest") != 0) { screen_msg("REMOVE CARD", "Not a guest", C_YELLOW); HAL_Delay(2000); return; }

  const char *opts[2] = { "YES", "NO" };
  int sel = menu_select("DELETE CARD?", opts, 2, 1);   /* default NO */
  if (sel != 0) return;

  screen_msg("REMOVE CARD", "Removing...", C_WHITE);
  send_cmd("remove_card", uid, -1);
  r = wait_resp("/remove_card", extra, 8000);
  if (r == 1) screen_msg("SUCCESS", "Card removed", C_GREEN);
  else        screen_msg("FAILED", r == 0 ? "Remove failed" : "No response", C_RED);
  HAL_Delay(2500);
}

/* ---- Mode 4: check balance ---- */
static void mode_check_balance(void) {
  char uid[16], extra[32];
  if (!wait_card("CHECK BALANCE", uid, 10000)) {
    screen_msg("CHECK BALANCE", "No card", C_YELLOW); HAL_Delay(1500); return;
  }
  screen_msg("CHECK BALANCE", "Checking...", C_WHITE);
  send_cmd("check_balance", uid, -1);
  int r = wait_resp("/check_balance", extra, 6000);
  if (r == 1)      { char m[32]; sprintf(m, "%s COINS", extra); screen_msg("BALANCE", m, C_GREEN); }
  else if (r == 0) { screen_msg("CHECK BALANCE", "Not in system", C_RED); }
  else             { screen_msg("CHECK BALANCE", "No response", C_RED); }
  HAL_Delay(2500);
}

/* Boot self-test: ping the ESP32 and show whether the WiFi/MQTT chain is up. */
static void check_link(void) {
  char extra[32];
  screen_msg("TOP-UP STATION", "Checking link...", C_WHITE);
  send_cmd("ping", "", -1);
  int r = wait_resp("/ping", extra, 4000);
  if (r == 1)      screen_msg("LINK OK", "ESP32 + MQTT up", C_GREEN);
  else if (r == 0) screen_msg("LINK WARN", "Network down", C_YELLOW);
  else             screen_msg("NO ESP32", "Check UART/power", C_RED);
  HAL_Delay(1800);
}

void App_Init(void) {
  LCD_Init();
  check_link();
  draw_menu("TOP-UP STATION", HOME_OPTS, 4, 0);
}

void App_Task(void) {
  static int sel = 0;
  btn_t b = get_btn();
  if (b == BTN_NEXT) {
    int old = sel;
    sel = (sel + 1) % 4;
    menu_move(HOME_OPTS, old, sel);
  } else if (b == BTN_SELECT) {
    if (sel == 0)      mode_add_money();
    else if (sel == 1) mode_create_guest();
    else if (sel == 2) mode_remove_card();
    else               mode_check_balance();
    draw_menu("TOP-UP STATION", HOME_OPTS, 4, sel);
  }
  HAL_Delay(20);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
