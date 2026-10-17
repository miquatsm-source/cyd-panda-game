# CYD Panda Game

A small touchscreen game for an ESP32-based CYD board. Guide the green panda
across a grassy field, collect bread, and avoid the red bugs chasing you. The
field contains walls, the bugs move on their own, and a buzzer plays sound
effects and music.

## How to play

- Tap the screen to send the green panda toward that position.
- Collect 21 pieces of bread to win.
- Avoid the red bugs. More red bugs join the chase as bread is collected.
- Bread can also be collected by a red bug; every pickup advances the counter.
- Use the `-` and `+` controls in the upper-right corner to change the volume.
- If caught, tap **New Game** to start over.

## Hardware

This project is configured for an **ESP32 CYD (Cheap Yellow Display) board
with a 2.8-inch, 240 x 320 touchscreen configured to use the ST7789 display
driver**. The firmware also uses the CYD's XPT2046 touch controller and the
buzzer on GPIO 26.

CYD boards are sold in multiple hardware revisions with different display
controllers and pin layouts. Check the controller printed in your board's
specifications before uploading: this project uses the ST7789 setup in
`include/cyd_user_setup.h`. A board with a different display controller may
need a matching TFT_eSPI setup before the display will work.

## Install and upload

### 1. Install the tools

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Install the **PlatformIO IDE** extension from the VS Code Extensions view.
3. Connect the CYD to your computer with a USB data cable.

### 2. Get the project

Clone this repository, then open the cloned project folder in VS Code:

```sh
git clone https://github.com/miquatsm-source/cyd-panda-game.git
```

PlatformIO reads `platformio.ini` and installs the required ESP32 platform and
libraries when the project is built for the first time.

### 3. Build and upload

In VS Code, open **PlatformIO: Build** and then **PlatformIO: Upload** from the
PlatformIO toolbar or command palette. The configured default environment is
`esp32dev-fast`.

Alternatively, run these commands in a terminal opened in the project folder:

```sh
pio run -e esp32dev-fast
pio run -e esp32dev-fast -t upload
```

If PlatformIO cannot find the board's serial port, select the correct port in
PlatformIO or connect a data-capable USB cable. Some ESP32 boards require
holding the **BOOT** button while upload starts.

## First startup and touch calibration

On first startup, the game asks you to calibrate the touchscreen. Touch and
release each of the four targets as they appear. The calibration is saved in
the ESP32's preferences and reused on later starts.

To calibrate again, open the PlatformIO Serial Monitor at **115200 baud** and
send `c`. The monitor also displays game and diagnostic messages.

## Build configuration

- PlatformIO environment: `esp32dev-fast` (default)
- Framework: Arduino for ESP32
- Display library: TFT_eSPI
- Touch library: XPT2046_Touchscreen
- Display and pin configuration: `include/cyd_user_setup.h`
