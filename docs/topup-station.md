# Top-up Station — app flow & protocol

STM32 firmware turns the terminal into a card top-up / card-management station.
Three modes, driven by **2 buttons** and shown on the **ILI9341 LCD (SPI3)**.
Card UID comes from the RC522 (SPI1). All DB logic lives in the Go backend
(`mad-tetris-backend`); the ESP32 only bridges UART ↔ MQTT.

```
[RC522] --SPI1--> [STM32] --SPI3--> [LCD]
                     |  UART2 (CMD / RESP)
                     v
                  [ESP32] --MQTT/TLS--> [HiveMQ] <--> [Go backend] <--> [Postgres]
```

## Buttons

Only two buttons, used the same way on every screen:

- **BTN1 = NEXT** — move the highlight / cycle options
- **BTN2 = OK** — select the highlighted option

EXTI, internal pull-up, active-low, 200 ms debounce (see `hardware-wiring.md`).

## Modes

Home menu: `ADD MONEY`, `NEW GUEST`, `REMOVE CARD`, `CHECK BALANCE`.

### 1. ADD MONEY
1. Wait for a card tap, **10 s** countdown. No card → back home.
2. Card tapped → `check_role`. Not in system → "Not in system" → home.
3. In system → amount menu: **20 / 50 / 100 / CANCEL**.
4. Pick amount → `add_money` → show SUCCESS/FAILED → home. CANCEL → home.

### 2. NEW GUEST (register)
1. Wait for a card tap, 10 s. No card → home.
2. Card tapped → `check_role`. **Already registered → "Already exists" → home.**
3. Not registered → `create_guest` → show SUCCESS/FAILED → home.

### 3. REMOVE CARD (guest)
1. Wait for a card tap, 10 s. No card → home.
2. Card tapped → `check_role`. Not in system → "Not in system" → home.
   Card is a member (not guest) → "Not a guest" → home.
3. Guest → confirm menu **YES / NO** (default NO).
4. YES → `remove_card` → show SUCCESS/FAILED → home. NO → home.

### 4. CHECK BALANCE
1. Wait for a card tap, 10 s. No card → home.
2. Card tapped → `check_balance`. Not in system → "Not in system" → home.
3. In system → show "`<balance>` COINS" → home.

> LCD text is **English/ASCII only** — the built-in 5x7 font has no Thai glyphs.

## UART line protocol (STM32 ↔ ESP32, 115200 8N1)

One line per message, `\n`-terminated.

### STM32 → ESP32: `CMD`
```
CMD <action> <uid> [amount]
```
| action         | amount | example                         |
|----------------|--------|---------------------------------|
| `check_role`   | no     | `CMD check_role A1B2C3D4`       |
| `add_money`    | yes    | `CMD add_money A1B2C3D4 50`     |
| `create_guest` | no     | `CMD create_guest A1B2C3D4`     |
| `remove_card`  | no     | `CMD remove_card A1B2C3D4`      |
| `check_balance`| no     | `CMD check_balance A1B2C3D4`    |
| `ping`         | no uid | `CMD ping` — connection self-test |

`uid` = 8 hex chars (4-byte RC522 UID).

### ESP32 → STM32: `RESP`
```
RESP <topic> OK|ERR [extra]
```
`<topic>` matches the request so the STM32 can correlate; it ignores lines whose
topic ≠ the one it is waiting on, and times out otherwise.

| topic           | OK form                 | ERR form                          |
|-----------------|-------------------------|-----------------------------------|
| `/check_role`   | `OK member` / `OK guest`| `ERR invalid_card`                |
| `/add_money`    | `OK 50`                 | `ERR invalid_card`                |
| `/create_guest` | `OK`                    | `ERR card_already_exists`         |
| `/remove_card`  | `OK`                    | `ERR invalid_card`                |
| `/check_balance`| `OK 120`                | `ERR invalid_card`                |
| `/ping`         | `OK wifi=1,mqtt=1`      | `ERR nowifi` / `ERR nomqtt`       |

On boot the STM32 sends `CMD ping` and shows **LINK OK / LINK WARN / NO ESP32**
for ~1.8 s before the home menu. The ESP32 replies `OK` only when both WiFi and
MQTT are up; if a real command arrives while MQTT is down it answers
`RESP <topic> ERR no_mqtt` immediately so the terminal doesn't wait for timeout.

The STM32 only checks the `OK`/`ERR` word (and `extra` for the role in
`check_role`); `extra` on errors is a human token, not parsed.

## ESP32 ↔ MQTT (HiveMQ) mapping

ESP32 publishes JSON to the base topic and subscribes to `<topic>/response`.

| action       | publish topic   | payload                                  |
|--------------|-----------------|------------------------------------------|
| check_role   | `/check_role`   | `{"card_id":"<uid>"}`                    |
| add_money    | `/add_money`    | `{"card_id":"<uid>","amount":<n>}`       |
| create_guest | `/create_guest` | `{"card_id":"<uid>"}`                    |
| remove_card  | `/remove_card`  | `{"card_id":"<uid>"}`                    |
| check_balance| `/check_balance`| `{"card_id":"<uid>"}`                    |

Backend replies on `<topic>/response` (see `mad-tetris-backend/services/
message_service.go`). The ESP32 parses that JSON and emits the `RESP` line:
`"error"` present → `ERR`; otherwise `OK` plus role (`check_role`), amount
(`add_money`), or balance (`check_balance`).

## Firmware layout

- `Core/Src/ili9341.c` / `Core/Inc/ili9341.h` — LCD driver (SPI3, 5x7 font).
- `Core/Src/main.c` `/* USER CODE 4 */` — app state machine, screens, UART
  `CMD`/`RESP` helpers, RC522 read, button EXTI callback.
- `arduino/top_up_station/top_up_station.ino` — ESP32 UART↔MQTT bridge.

## Build / flash notes

1. `payment_mqtt.ioc` declares SPI3, the button pins (GPXTI), and the LCD GPIOs;
   CubeMX generates `hspi3`, `MX_SPI3_Init`, and the `*_Pin` / `*_GPIO_Port`
   macros. **Button EXTI is set up by hand in `main.c` USER CODE**, not by the
   GUI: `MX_GPIO_Init` only configures the pins, so after generation the code in
   `USER CODE 2` forces falling-edge + enables `EXTI0_IRQn` / `EXTI9_5_IRQn`, and
   `EXTI0_IRQHandler` / `EXTI9_5_IRQHandler` live in `USER CODE 4`.
   **Do NOT enable "EXTI line0" / "EXTI line[9:5]" in the CubeMX NVIC tab** — the
   generated handlers in `stm32l4xx_it.c` would then clash with the ones in
   `main.c`. (The CubeMX GUI keeps resetting the pins to rising-edge / NVIC-off
   on every regen; the hand-written setup makes that harmless.)
2. Build (`cd Debug && make -j`) and flash.
3. Flash the ESP32 with `arduino/top_up_station/top_up_station.ino`
   (Arduino IDE, board = ESP32; libs: `PubSubClient`). Set Wi-Fi/MQTT creds.
4. Run the backend + broker so MQTT responses come back.
