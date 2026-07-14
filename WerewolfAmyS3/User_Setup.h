// TFT_eSPI User_Setup for WerewolfAmyS3
// ESP32-S3-WROOM DevKitC + 3.2" ILI9341 320x240 SPI module
//
// Arduino IDE: copy this file over  <libraries>/TFT_eSPI/User_Setup.h
// PlatformIO: not needed — platformio.ini passes the same values as -D flags.

#define USER_SETUP_INFO "WerewolfAmyS3"

#define ILI9341_DRIVER

// ESP32-S3 FSPI pins (native IOMUX pins for top speed)
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST  14
// Backlight is driven directly from the sketch (GPIO 21)

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

#define SUPPORT_TRANSACTIONS
