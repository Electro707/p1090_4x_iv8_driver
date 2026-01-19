// must come before <FastLED.h>
// due to how the RMI interrupts, the SPI backup seems to have less flicker as it uses DMA
#define FASTLED_ALLOW_INTERRUPTS 0
#define FASTLED_ESP32_USE_CLOCKLESS_SPI
#define FASTLED_ESP32_SPI_BUS   HSPI        // use a different SPI than the shift register's VSPI

// #include <SPI.h>
#include <FastLED.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <HardwareSerial.h>
#include <time.h>
#include "common.h"
#include "soc/spi_struct.h"
#include "soc/gpio_struct.h"
#include "soc/dport_reg.h"
/**
 * The file below must be created, which defines DEFAULT_WIFI_SSID and DEFAULT_WIFI_PASSWORD
 */
#include "wifiDefault.h"
#include "comms.h"

char wifiSsid[32];
char wifiPassword[32];

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -5*3600;      // Set your timezone offset (e.g., -5*3600 for EST)
const int   daylightOffset_sec = 3600; // Daylight savings (if applicable)

// a look-up betwen display number and enable IO
const uint8_t ioVfdEn[N_DISPLAYS] = {19, 21, 22, 23};
// a look-up between a number to be display and the 7-segment settings
const uint8_t numberToSeg[10] = {0xb7, 0x14, 0x73, 0x76, 0xd4, 0xe6, 0xe7, 0x34, 0xf7, 0xf6};

hw_timer_t *mainTimer;

uint currDisplayedN = 0;     // the current number being displayed. Only to be updated in displayNumber
uint8_t segmentsEnabled[N_DISPLAYS];    // what segments are enabled/on per display

portMUX_TYPE segEnMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE ledSpinLock = portMUX_INITIALIZER_UNLOCKED;

struct tm currTime;

EEPROMClass nvm("main");

struct led_s leds;

NetworkServer networkServer(23);
NetworkClient networkClient;        // for now only allow one client

HardwareSerial commsSerial(1);

// parser struct for our custom parser
ParserHandler serialParser;
ParserHandler networkParser;

dispMode_e dispMode;
timeFormat_e timeFormat;

TimerHandle_t updateALedT;
TimerHandle_t updateTimeT;

/**
 * This function, called from an interrupt, updates one display at a time every 2Khz
 */
void IRAM_ATTR segmentInterrupt(void){
    static uint32_t currDisp = 0;        // a counter for the current display enabled

    // if we are running ws2812b updates, just bail for this cycle


    // turn off last display
    digitalWrite(ioVfdEn[currDisp], LOW);
    
    // transistion to next display
    currDisp++;
    currDisp &= 0x03;  // equivalent to currDisp %= 4, boolean math magic

    digitalWrite(IO_SHIFT_LDR, LOW);
    // vspi.transfer(segmentsEnabled[currDisp]);

    SPI3.mosi_dlen.usr_mosi_dbitlen = 7;
    SPI3.miso_dlen.usr_miso_dbitlen = 7;

    portENTER_CRITICAL_ISR(&segEnMux);
    SPI3.data_buf[0] = segmentsEnabled[currDisp];
    portEXIT_CRITICAL_ISR(&segEnMux);

    SPI3.cmd.usr = 1;
    while(SPI3.cmd.usr != 0);

    digitalWrite(IO_SHIFT_LDR, HIGH);
    digitalWrite(ioVfdEn[currDisp], HIGH);
}

void setup() {
    Serial.begin(115200);
    // todo: a bit cursed, maybe it will be better to directly handle receive commands.
    //       increasing buffer size for high speed firmware updates
    commsSerial.setRxBufferSize(MAX_FW_BUFFER);
    commsSerial.begin(115200, SERIAL_8N1, COMMS_UART_RX, COMMS_UART_TX);
    DEBUG("initializing");

    // IO init
    pinMode(IO_HIV_EN, OUTPUT);
    pinMode(IO_SHIFT_OE, OUTPUT);
    pinMode(IO_SHIFT_LDR, OUTPUT);
    pinMode(IO_SHIFT_RST, OUTPUT);
    for(int i=0;i<N_DISPLAYS;i++){
        digitalWrite(ioVfdEn[i], LOW);
        pinMode(ioVfdEn[i], OUTPUT);
    }
    // ensure this is HIGH on startup
    digitalWrite(IO_SHIFT_OE, HIGH);

    if (!nvm.begin(0x200)) {
        // todo: this came from their example. is restarting the best?
        DEBUG("Failed to initialize nvm");
        DEBUG("Restarting...");
        delay(1000);
        ESP.restart();
    }
    
    // we have an empty nvm, initialize
    if(nvm.read(0) != NVM_MAGIC){
        strcpy(wifiSsid, DEFAULT_WIFI_SSID);
        strcpy(wifiPassword, DEFAULT_WIFI_PASSWORD);
    }

    // set variables
    memset(segmentsEnabled, 0x40, sizeof(segmentsEnabled));
    leds.ledBlinkMode = LED_BLINK_MODE_SEC;
    leds.ledColorMode = LED_MODE_TIME_HUE;
    timeFormat = TIME_FORMAT_24HR;
    
    // vspi.begin(IO_SHIFT_CLK, -1, IO_SHIFT_DAT, -1);
    // vspi.setHwCs(false);
    // vspi.beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));

    GPIO.enable |= (1 << IO_SHIFT_CLK) | (1 << IO_SHIFT_DAT);
    GPIO.func_out_sel_cfg[IO_SHIFT_DAT].val = 65 | (1 << 10);
    GPIO.func_out_sel_cfg[IO_SHIFT_CLK].val = 63 | (1 << 10);
    REG_WRITE(IO_MUX_GPIO25_REG, 0x2801);
    REG_WRITE(IO_MUX_GPIO26_REG, 0x2801);

    DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_SPI3_CLK_EN);
    SPI3.pin.val = 0b111;
    SPI3.pin.ck_idle_edge = 1;
    SPI3.user.usr_command = 0;
    SPI3.user.usr_mosi = 1;
    SPI3.user.doutdin = 1;

    SPI3.ctrl2.miso_delay_mode = 2;
    SPI3.clock.clkdiv_pre = 59;
    SPI3.clock.clkcnt_n = 3;
    SPI3.clock.clkcnt_h = 1;
    SPI3.clock.clkcnt_l = 3;
    SPI3.clock.clk_equ_sysclk = 0;

    // reset shift register
    digitalWrite(IO_SHIFT_RST, LOW);
    digitalWrite(IO_SHIFT_LDR, LOW);
    delay(10);
    digitalWrite(IO_SHIFT_RST, HIGH);
    digitalWrite(IO_SHIFT_LDR, HIGH);
    digitalWrite(IO_SHIFT_OE, LOW);
    // enable high voltage boost converter
    digitalWrite(IO_HIV_EN, HIGH);

    // setup wifi
    WiFi.setHostname(NETWORK_HOSTNAME);
    WiFi.onEvent(WiFiEvent);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.begin(wifiSsid, wifiPassword);

    // setup LEDs
    FastLED.addLeds<WS2812, IO_ADDR_LED>(leds.leds, NUM_ADDR_LEDS);
    
    // setup comms
    serialParser.setPrintClass(&commsSerial);
    // network setup happens on connection

    // setup tasks
    // because the updating of the segments is critical, we use an old fashioned interrupt    
    mainTimer = timerBegin(1000000);
    timerAttachInterrupt(mainTimer, &segmentInterrupt);
    timerAlarm(mainTimer, 500, true, 0);
    // for the rest of the stuff, they can run with a decent amount of jitter/deviation
    updateTimeT = xTimerCreate("updateTimeT", pdMS_TO_TICKS(250), true, NULL, updateTimeCallback);
    updateALedT = xTimerCreate("updateALedT", pdMS_TO_TICKS(50), true, NULL, updateLED);
    xTimerStart(updateALedT, 0);

    setDisplayMode(DISPLAY_MODE_TIME);

    DEBUG("postBegin");
}

// void onConnect(arduino_event_t *e){
//     Serial.print("Obtained IP address: ");
//     Serial.println(WiFi.localIP());

//     configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
// }

void WiFiEvent(WiFiEvent_t event) {
    // todo: clean up the case statements as needed
    DEBUG("[WiFi-event] event: %d\n", event);

    switch (event) {
        case ARDUINO_EVENT_WIFI_READY:               DEBUG("WiFi interface ready"); break;
        case ARDUINO_EVENT_WIFI_SCAN_DONE:           DEBUG("Completed scan for access points"); break;
        case ARDUINO_EVENT_WIFI_STA_START:           DEBUG("WiFi client started"); break;
        case ARDUINO_EVENT_WIFI_STA_STOP:            DEBUG("WiFi clients stopped"); break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:       DEBUG("Connected to access point"); break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:    DEBUG("Disconnected from WiFi access point"); break;
        case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE: DEBUG("Authentication mode of access point has changed"); break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            DEBUG("Obtained IP address: %s", WiFi.localIP().toString().c_str());
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            networkServer.begin();
            networkServer.setNoDelay(true);
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            DEBUG("Lost IP address and IP address is reset to 0");
            networkServer.end();
            break;
        case ARDUINO_EVENT_WPS_ER_SUCCESS:          DEBUG("WiFi Protected Setup (WPS): succeeded in enrollee mode"); break;
        case ARDUINO_EVENT_WPS_ER_FAILED:           DEBUG("WiFi Protected Setup (WPS): failed in enrollee mode"); break;
        case ARDUINO_EVENT_WPS_ER_TIMEOUT:          DEBUG("WiFi Protected Setup (WPS): timeout in enrollee mode"); break;
        case ARDUINO_EVENT_WPS_ER_PIN:              DEBUG("WiFi Protected Setup (WPS): pin code in enrollee mode"); break;

        case ARDUINO_EVENT_WIFI_AP_START:           DEBUG("WiFi access point started"); break;
        case ARDUINO_EVENT_WIFI_AP_STOP:            DEBUG("WiFi access point  stopped"); break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    DEBUG("Client connected"); break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: DEBUG("Client disconnected"); break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:   DEBUG("Assigned IP address to client"); break;
        case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:  DEBUG("Received probe request"); break;
        case ARDUINO_EVENT_WIFI_AP_GOT_IP6:         DEBUG("AP IPv6 is preferred"); break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP6:        DEBUG("STA IPv6 is preferred"); break;
        default:                                    break;
    }
}

void loop() {
    // handle new client connections
    if (networkServer.hasClient()) {
        if(networkClient.connected()){
            networkServer.accept().stop();
        } else {
            networkClient = networkServer.accept();
            networkParser.setPrintClass(&networkClient);
            DEBUG("New client: %s", networkClient.remoteIP().toString().c_str());
        }
    }
    // client read loop
    if(networkClient.connected()){
        while(networkClient.available()){
            networkParser.parse(networkClient.read());
        } 
    }

    while(commsSerial.available()){
        serialParser.parse(commsSerial.read());
    }
}

// handles the transition in the display mode
void setDisplayMode(dispMode_e newMode){
    dispMode = newMode;
    
    if(newMode == DISPLAY_MODE_TIME){
        xTimerStart(updateTimeT, 0);
    } else {
        xTimerStop(updateTimeT, 0);
    }

    if(newMode == DISPLAY_MODE_OFF){
        portENTER_CRITICAL(&segEnMux);
        for(int i=0;i<N_DISPLAYS;i++){
            segmentsEnabled[i] = 0;
        }
        portEXIT_CRITICAL(&segEnMux);
    }
}

void updateTimeCallback(TimerHandle_t xTimer){
    uint currTimeN;
    time_t now;
    float tmp;

    // basically doing the same as getLocalTime in `esp32-hal-time.c`, but no timeout. if it fails it fails
    time(&now);
    localtime_r(&now, &currTime);
    if (currTime.tm_year > (2016 - 1900)) {
        switch(timeFormat){
            case TIME_FORMAT_24HR:
                currTimeN = currTime.tm_min + (currTime.tm_hour*100);
                break;
            case TIME_FORMAT_12HR:
                currTimeN = currTime.tm_min + ((currTime.tm_hour % 12)*100);
                break;
            case TIME_FORMAT_METRIC:
                // get the current time as a proportion of the day
                tmp = currTime.tm_sec + (60.0*(float)currTime.tm_min) + (3600.0*(float)currTime.tm_hour);
                tmp /= 86400;
                // now start dividing and filling the hours and minutes spot
                tmp *= 10;
                currTimeN = (uint)floor(tmp);
                tmp -= currTimeN;
                currTimeN *= 100;       // convert the hours to the 100's decimal place
                tmp *= 100;
                currTimeN += (uint)floor(tmp);
                break;
        }
        
        displayNumber(currTimeN);
    }
}

void updateLED(TimerHandle_t xTimer){
    static CHSV toSet = CHSV(0, 255, 128);
    CRGB toSetRgb;
    float tmp;

    switch(leds.ledColorMode){
        case LED_MODE_RAINBOW:
            toSet.h += 1;
            hsv2rgb_spectrum(toSet, toSetRgb);
            fill_solid(leds.leds, NUM_ADDR_LEDS, toSetRgb);
            break;
        case LED_MODE_TIME_HUE:
            tmp = (float)currTime.tm_sec;
            tmp /= 60;
            tmp *= 256;
            toSet.h = (uint8_t)tmp;
            hsv2rgb_spectrum(toSet, toSetRgb);
            fill_solid(leds.leds, NUM_ADDR_LEDS, toSetRgb);
            break;
        default:
            break;
    }

    switch(leds.ledBlinkMode){
        case LED_BLINK_MODE_OFF:
            break;
        case LED_BLINK_MODE_SEC:
            // turn the whole LED set off once every other odd second
            if((currTime.tm_sec % 2) == 0){
                fill_solid(leds.leds, NUM_ADDR_LEDS, CRGB::Black);
            }
            break;
        default:
            break;
    }

    taskENTER_CRITICAL(&ledSpinLock);
    FastLED.show();
    taskEXIT_CRITICAL(&ledSpinLock);
}

// displays a new number
void displayNumber(uint n){
    const uint power10[N_DISPLAYS] = {1, 10, 100, 1000};
    uint toS;

    portENTER_CRITICAL(&segEnMux);
    for(int i=0;i<N_DISPLAYS;i++){
        // only remove the zeros if we are displaying a number (todo: maybe just exclude time from this)
        if(dispMode == DISPLAY_MODE_NUMB){
            if(n < power10[i]){
                segmentsEnabled[i] = 0;
                continue;
            }
        }
        toS = n / power10[i];
        toS %= 10;
        segmentsEnabled[i] = numberToSeg[toS];
    }
    portEXIT_CRITICAL(&segEnMux);

    currDisplayedN = n;      // update global variable
}

void saveNvm(void){
    nvm.writeByte(0, NVM_MAGIC);
    nvm.writeBytes(1, wifiSsid, 32);
    nvm.writeBytes(1, wifiPassword, 32);
    nvm.commit();
}