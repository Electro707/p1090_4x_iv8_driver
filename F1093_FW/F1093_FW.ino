#include <SPI.h>
#include <FastLED.h>
#include <WiFi.h>
#include "time.h"

#define IO_HIV_EN           12

#define IO_SHIFT_OE         33
#define IO_SHIFT_LDR        32
#define IO_SHIFT_RST        27
#define IO_SHIFT_CLK        26
#define IO_SHIFT_DAT        25

#define IO_ADDR_LED         14

#define NUM_ADDR_LEDS       4

// Replace with your network credentials
const char* wifiSsid       = "Kati 2.4Ghz";
const char* wifiPassword   = "KBJBNB1717";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -5*3600;      // Set your timezone offset (e.g., -5*3600 for EST)
const int   daylightOffset_sec = 3600; // Daylight savings (if applicable)

const uint8_t ioVfdEn[4] = {19, 21, 22, 23};
const uint8_t numberToSeg[10] = {0xb7, 0x14, 0x73, 0x76, 0xd4, 0xe6, 0xe7, 0x34, 0xf7, 0xf6};

static const int spiClk = 1000000;  // 1 MHz
SPIClass vspi = SPIClass(VSPI);

hw_timer_t *mainTimer = timerBegin(1000000);

unsigned int currN = 0;

CRGB leds[NUM_ADDR_LEDS];

void setup() {
    Serial.begin(115200);

    digitalWrite(IO_SHIFT_OE, HIGH);

    pinMode(IO_HIV_EN, OUTPUT);
    pinMode(IO_SHIFT_OE, OUTPUT);
    pinMode(IO_SHIFT_LDR, OUTPUT);
    pinMode(IO_SHIFT_RST, OUTPUT);
    for(int i=0;i<4;i++){
        pinMode(ioVfdEn[i], OUTPUT);
    }

    vspi.begin(IO_SHIFT_CLK, -1, IO_SHIFT_DAT, -1);
    vspi.setHwCs(false);
    vspi.beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));

    digitalWrite(IO_SHIFT_RST, LOW);
    digitalWrite(IO_SHIFT_LDR, LOW);
    delay(10);
    digitalWrite(IO_SHIFT_RST, HIGH);
    // vspi.transfer(numberToSeg[0]);
    digitalWrite(IO_SHIFT_LDR, HIGH);
    digitalWrite(IO_SHIFT_OE, LOW);


    digitalWrite(ioVfdEn[3], HIGH);
    digitalWrite(IO_HIV_EN, HIGH);

    WiFi.onEvent(WiFiEvent);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname("ESP32-IV8");
    WiFi.begin(wifiSsid, wifiPassword);

    timerAttachInterrupt(mainTimer, &segmentInterrupt);
    timerAlarm(mainTimer, 500, true, 0);

    FastLED.addLeds<WS2812, IO_ADDR_LED>(leds, NUM_ADDR_LEDS);

    Serial.println("postBegin");
}

// void onConnect(arduino_event_t *e){
//     Serial.print("Obtained IP address: ");
//     Serial.println(WiFi.localIP());

//     configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
// }

void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);

  switch (event) {
    case ARDUINO_EVENT_WIFI_READY:               Serial.println("WiFi interface ready"); break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:           Serial.println("Completed scan for access points"); break;
    case ARDUINO_EVENT_WIFI_STA_START:           Serial.println("WiFi client started"); break;
    case ARDUINO_EVENT_WIFI_STA_STOP:            Serial.println("WiFi clients stopped"); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:       Serial.println("Connected to access point"); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:    Serial.println("Disconnected from WiFi access point"); break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE: Serial.println("Authentication mode of access point has changed"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("Obtained IP address: ");
      Serial.println(WiFi.localIP());
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:        Serial.println("Lost IP address and IP address is reset to 0"); break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:          Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_FAILED:           Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:          Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_PIN:              Serial.println("WiFi Protected Setup (WPS): pin code in enrollee mode"); break;
    case ARDUINO_EVENT_WIFI_AP_START:           Serial.println("WiFi access point started"); break;
    case ARDUINO_EVENT_WIFI_AP_STOP:            Serial.println("WiFi access point  stopped"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    Serial.println("Client connected"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: Serial.println("Client disconnected"); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:   Serial.println("Assigned IP address to client"); break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:  Serial.println("Received probe request"); break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:         Serial.println("AP IPv6 is preferred"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:        Serial.println("STA IPv6 is preferred"); break;
    case ARDUINO_EVENT_ETH_GOT_IP6:             Serial.println("Ethernet IPv6 is preferred"); break;
    case ARDUINO_EVENT_ETH_START:               Serial.println("Ethernet started"); break;
    case ARDUINO_EVENT_ETH_STOP:                Serial.println("Ethernet stopped"); break;
    case ARDUINO_EVENT_ETH_CONNECTED:           Serial.println("Ethernet connected"); break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:        Serial.println("Ethernet disconnected"); break;
    case ARDUINO_EVENT_ETH_GOT_IP:              Serial.println("Obtained IP address"); break;
    default:                                    break;
  }
}

void loop() {
    static unsigned long lastT = 0;
    static unsigned long lastT2 = 0;
    static CHSV toSet = CHSV(0, 255, 128);
    static CRGB toSetRgb;
  // put your main code here, to run repeatedly:
    // for(int i=0;i<10;i++){
    //     digitalWrite(IO_SHIFT_LDR, LOW);
    //     // vspi.transfer(1 << i);
    //     vspi.transfer(numberToSeg[i]);
    //     digitalWrite(IO_SHIFT_LDR, HIGH);
    //     delay(250);
    // }
    if(millis() - lastT2 > 500){
        lastT2 = millis();
        struct tm t;
        if(getLocalTime(&t, 100)){
            currN = t.tm_min + (t.tm_hour*100);
        }
    }

    if(millis() - lastT > 40){
        lastT = millis();
        toSet.h += 1;
        hsv2rgb_spectrum(toSet, toSetRgb);
        fill_solid(leds, NUM_ADDR_LEDS, toSetRgb);
        FastLED.show();
    }
}

uint8_t currDisp = 0;
const uint divideByLUT[4] = {1, 10, 100, 1000};
void segmentInterrupt(void){
    for(int i=0;i<4;i++){
        digitalWrite(ioVfdEn[i], LOW);
    }

    unsigned int toS;

    toS = currN / divideByLUT[currDisp];
    toS %= 10;

    digitalWrite(IO_SHIFT_LDR, LOW);
    vspi.transfer(numberToSeg[toS]);
    digitalWrite(IO_SHIFT_LDR, HIGH);

    digitalWrite(ioVfdEn[currDisp], HIGH);

    currDisp += 1;
    currDisp %= 4;
}
