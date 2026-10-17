#pragma once

#define USER_SETUP_INFO "CYD 2.8 inch ST7789"
#define ST7789_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_ROTATION 1

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21
#define TFT_BACKLIGHT_ON 1
#define TOUCH_CS 33

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY 40000000
#endif
#define SPI_READ_FREQUENCY 20000000

#define LOAD_GLCD
#define LOAD_FONT2

#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_BGR
