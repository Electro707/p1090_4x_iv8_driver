#include <SPI.h>
#include <FastLED.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <HardwareSerial.h>
#include <time.h>
#include "common.h"
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

const int spiClk = 1000000;  // 1 MHz
SPIClass vspi = SPIClass(VSPI);

hw_timer_t *mainTimer = timerBegin(1000000);

uint currDisplayedN = 0;     // the current number being displayed. Only to be updated in displayNumber
uint8_t segmentsEnabled[N_DISPLAYS];    // what segments are enabled/on per display

struct tm currTime;

EEPROMClass nvm("main");

CRGB leds[NUM_ADDR_LEDS];

NetworkServer networkServer(23);
NetworkClient networkClient;        // for now only allow one client

HardwareSerial commsSerial(1);

// parser struct for our custom parser
ParserHandler serialParser;
ParserHandler networkParser;

dispMode_e dispMode;

TimerHandle_t updateTimeT;
TimerHandle_t updateALedT;

void setup() {
    Serial.begin(115200);
    // todo: a bit cursed, maybe it will be better to directly handle receive commands.
    //       increasing buffer size for high speed firmware updates
    commsSerial.setRxBufferSize(MAX_FW_BUFFER);
    commsSerial.begin(115200, SERIAL_8N1, COMMS_UART_RX, COMMS_UART_TX);

    // IO init
    pinMode(IO_HIV_EN, OUTPUT);
    pinMode(IO_SHIFT_OE, OUTPUT);
    pinMode(IO_SHIFT_LDR, OUTPUT);
    pinMode(IO_SHIFT_RST, OUTPUT);
    for(int i=0;i<4;i++){
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

    memset(segmentsEnabled, 0, sizeof(segmentsEnabled));

    vspi.begin(IO_SHIFT_CLK, -1, IO_SHIFT_DAT, -1);
    vspi.setHwCs(false);
    vspi.beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));

    digitalWrite(IO_SHIFT_RST, LOW);
    digitalWrite(IO_SHIFT_LDR, LOW);
    delay(10);
    digitalWrite(IO_SHIFT_RST, HIGH);
    digitalWrite(IO_SHIFT_LDR, HIGH);
    digitalWrite(IO_SHIFT_OE, LOW);

    digitalWrite(IO_HIV_EN, HIGH);

    WiFi.setHostname(NETWORK_HOSTNAME);
    WiFi.onEvent(WiFiEvent);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.begin(wifiSsid, wifiPassword);

    timerAttachInterrupt(mainTimer, &segmentInterrupt);
    timerAlarm(mainTimer, 500, true, 0);

    FastLED.addLeds<WS2812, IO_ADDR_LED>(leds, NUM_ADDR_LEDS);

    serialParser.setPrintClass(&commsSerial);

    updateTimeT = xTimerCreate("updateTimeT", pdMS_TO_TICKS(500), true, NULL, updateTimeCallback);
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
    // static unsigned long lastT = 0;
    // static unsigned long lastT2 = 0;
    // static CHSV toSet = CHSV(0, 255, 128);
    // static CRGB toSetRgb;
  // put your main code here, to run repeatedly:
    // for(int i=0;i<10;i++){
    //     digitalWrite(IO_SHIFT_LDR, LOW);
    //     // vspi.transfer(1 << i);
    //     vspi.transfer(numberToSeg[i]);
    //     digitalWrite(IO_SHIFT_LDR, HIGH);
    //     delay(250);
    // }

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

    // todo: handle in thread?
    // if(millis() - lastT2 > 500){
    //     lastT2 = millis();
    //     struct tm t;
    //     if(getLocalTime(&t, 100)){
    //         currN = t.tm_min + (t.tm_hour*100);
    //     }
    // }

    // todo: handle in thread?
    // if(millis() - lastT > 40){
    //     lastT = millis();
    //     toSet.h += 1;
    //     hsv2rgb_spectrum(toSet, toSetRgb);
    //     fill_solid(leds, NUM_ADDR_LEDS, toSetRgb);
    //     FastLED.show();
    // }
}

// handles the transition in the display mode
void setDisplayMode(dispMode_e newMode){
    dispMode = newMode;
    
    if(newMode == DISPLAY_MODE_TIME){
        // xTimerStart(updateTimeT, 0);
    } else {
        // xTimerStop(updateTimeT, 0);
    }

    if(newMode == DISPLAY_MODE_OFF){
        for(int i=0;i<N_DISPLAYS;i++){
            segmentsEnabled[i] = 0;
        }
    }
}

void updateTimeCallback(TimerHandle_t xTimer){
    uint currTimeN;
    time_t now;

    // basically doing the same as getLocalTime in `esp32-hal-time.c`, but no timeout. if it fails it fails
    time(&now);
    localtime_r(&now, &currTime);
    if (currTime.tm_year > (2016 - 1900)) {
        currTimeN = currTime.tm_min + (currTime.tm_hour*100);
        displayNumber(currTimeN);
    }
}

void updateLED(TimerHandle_t xTimer){
    static CHSV toSet = CHSV(0, 255, 128);
    static CRGB toSetRgb;

    toSet.h += 1;
    hsv2rgb_spectrum(toSet, toSetRgb);
    fill_solid(leds, NUM_ADDR_LEDS, toSetRgb);
    FastLED.show();
}

// displays a new number
void displayNumber(uint n){
    const uint power10[N_DISPLAYS] = {1, 10, 100, 1000};
    uint toS;

    for(int i=0;i<N_DISPLAYS;i++){
        if(n < power10[i]){
            segmentsEnabled[i] = 0;
        }
        else {
            toS = n / power10[i];
            toS %= 10;
            segmentsEnabled[i] = numberToSeg[toS];
        }
    }

    currDisplayedN = n;      // update global variable
}

void saveNvm(void){
    nvm.writeByte(0, NVM_MAGIC);
    nvm.writeBytes(1, wifiSsid, 32);
    nvm.writeBytes(1, wifiPassword, 32);
    nvm.commit();
}


/**
 * This function, called from an interrupt, updates one display at a time every 2Khz
 */
void segmentInterrupt(void){
    static uint8_t currDisp = 0;        // a counter for the current display enabled

    // turn off last display
    digitalWrite(ioVfdEn[currDisp], LOW);
    
    // transistion to next display
    currDisp++;
    currDisp &= 0x03;  // equivalent to currDisp %= 4, boolean math magic

    digitalWrite(IO_SHIFT_LDR, LOW);
    vspi.transfer(segmentsEnabled[currDisp]);
    digitalWrite(IO_SHIFT_LDR, HIGH);
    digitalWrite(ioVfdEn[currDisp], HIGH);
}
