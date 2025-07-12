#ifndef COMMON_H
#define COMMON_H

#include <time.h>

/********** Defines **********/

// comment the below define out to disable debug print through the serial port
#define ENABLE_DEBUG

#define FW_VERSION "F1093 Rev 1-T3"

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
#define DEBUG(_X, ...)       Serial.printf((_X "\n"), ##__VA_ARGS__)
#endif

/********** Enums and Structs **********/

typedef enum{
    DISPLAY_MODE_OFF = 0,
    DISPLAY_MODE_NUMB,
    DISPLAY_MODE_TIME,
}dispMode_e;

/********** Exposed main functions and variables **********/

extern dispMode_e dispMode;
extern void setDisplayMode(dispMode_e newMode);
extern void displayNumber(uint n);

extern uint currDisplayedN;

extern NetworkClient networkClient;

extern struct tm currTime;

#endif