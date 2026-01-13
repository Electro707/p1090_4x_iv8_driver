#ifndef COMMON_H
#define COMMON_H

#include <time.h>
#include <FastLED.h>

/********** Defines **********/

// comment the below define out to disable debug print through the serial port
#define ENABLE_DEBUG
// comment below to debug over the existing USB serial, uncomment for over the tag-connect serial
// #define DEBUG_OVER_USB_SER

#define FW_VERSION "F1093 Rev 1-T6"

#define NETWORK_HOSTNAME "esp32-f1093"

#define COMMS_UART_TX   17
#define COMMS_UART_RX   18

#define IO_HIV_EN           12

#define IO_SHIFT_OE         33
#define IO_SHIFT_LDR        32
#define IO_SHIFT_RST        27
#define IO_SHIFT_CLK        26
#define IO_SHIFT_DAT        25

#define IO_ADDR_LED         14

#define NUM_ADDR_LEDS       4
#define N_DISPLAYS          4

#define NVM_MAGIC           0x5A

/********** Macros **********/

#ifndef ENABLE_DEBUG
    #define DEBUG(_X, ...)
#else
    #ifdef DEBUG_OVER_USB_SER
        #define DEBUG(_X, ...)       commsSerial.printf((_X "\n"), ##__VA_ARGS__)
    #else
        #define DEBUG(_X, ...)       Serial.printf((_X "\n"), ##__VA_ARGS__)
    #endif
#endif

/********** Enums and Structs **********/

typedef enum{
    DISPLAY_MODE_OFF = 0,
    DISPLAY_MODE_NUMB,
    DISPLAY_MODE_TIME,
}dispMode_e;

typedef enum{
    TIME_FORMAT_24HR,
    TIME_FORMAT_12HR,
    TIME_FORMAT_METRIC
}timeFormat_e;

typedef enum{
    LED_MODE_MANUAL,        // LED can be manually changed
    LED_MODE_RAINBOW,       // LED operates in a rainbow config
    LED_MODE_TIME_HUE,
}ledColorMode_e;

typedef enum{
    LED_BLINK_MODE_OFF,     // no blinking
    LED_BLINK_MODE_SEC,     // blink once a second
}ledBlinkMode_e;

struct led_s{
    CRGB leds[NUM_ADDR_LEDS];
    ledColorMode_e ledColorMode;
    ledBlinkMode_e ledBlinkMode;
};

/********** Exposed main functions and variables **********/

extern struct led_s leds;
extern dispMode_e dispMode;
extern timeFormat_e timeFormat;
extern void setDisplayMode(dispMode_e newMode);
extern void displayNumber(uint n);

extern uint currDisplayedN;

extern NetworkClient networkClient;

extern struct tm currTime;

#endif