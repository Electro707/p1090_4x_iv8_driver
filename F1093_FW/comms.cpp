#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include "common.h"
#include "comms.h"

#define UPDATE_NOCRYPT
#include <Update.h>

// yes this is shared for the two communication modes, which is OK
// only one should be updating at a time
uint8_t fwBuffer[MAX_FW_BUFFER];
uint fwBufferIdx;


ParserHandler::ParserHandler(void){
    msgIdx = 0;
    prevByteEscape = false;
    prevCmdWill = false;
    prevCmdDo = false;
    lastReceivedInterrupt = false;
    expectedFwBytes = 0;
}

void ParserHandler::setPrintClass(Print *pH){
    printHandler = pH;
}

// handles reception of a byte at a time
void ParserHandler::parse(char b){
    if(expectedFwBytes){
        handleFirmwareUpdate(b);
        return;
    }

    if(prevByteEscape){
        prevByteEscape = false;
        switch(b){
            case 0xFF:
                parseTelnetDetokenized(b);      // double escape 0xFF-0xFF is just 0xFF
                break;
            case 0xFB:
                prevCmdWill = true;
                break;
            case 0xFD:
                prevCmdDo = true;
                break;
            case 0xF4:      // ctrl-c
                lastReceivedInterrupt = true;
                break;
            default:
                DEBUG("unhandled telnet command %d", b);
                break;
        }
    }
    else if(prevCmdWill){
        prevCmdWill = false;
        DEBUG("Received WILL token: %d", b);
    }
    else if(prevCmdDo){
        prevCmdDo = false;
        DEBUG("Received DO token: %d", b);
        if(b == 0x06){
            printHandler->print("\xFF\xFB\x06");
            if(lastReceivedInterrupt){
                lastReceivedInterrupt = false;
                msgIdx = 0;
                printHandler->print("\r\n");
            }
            
        }
    }
    else{
        if(b == 0xFF){
            prevByteEscape = true;
        } else {
            parseTelnetDetokenized(b);
        }
        
    }
}

void ParserHandler::handleFirmwareUpdate(char b){
    fwBuffer[fwBufferIdx++] = b;
    if(--expectedFwBytes == 0){
        DEBUG("received fw bytes");
        // we are done, can ACK and call the update function
        Update.write(fwBuffer, fwBufferIdx);
        txAck();
        fwBufferIdx = 0;        // to prevent accidental future from the consumed buffer
    }
}

// handles receiving bytes after de-tokenized by the parser
void ParserHandler::parseTelnetDetokenized(char b){
    if(b == '\n' || b == '\r'){
        if(msgIdx){
            msg[msgIdx] = 0;
            processCommand();
            msgIdx = 0;
        }
    }
    else{
        if(msgIdx < MAX_MSG_LEN){
            msg[msgIdx++] = b;
        }
    }
}

void ParserHandler::txAck(void){
    printHandler->println("ok");
}

void ParserHandler::txNack(const char *errMsg){
    printHandler->print("error: ");
    printHandler->println(errMsg);
}

// a helper macro to get the next argument, and if empty print error message
// this assumes the char* variable is named `token`
#define MACRO_GET_NEXTARG(_ERR_MSG) \
    token = strtok(NULL, " "); \
    if(token == NULL){ \
            txNack(_ERR_MSG);    \
            return; \
    }

void ParserHandler::processCommand(void){
    DEBUG("<- %s", msg);

    char *token = strtok(msg, " ");

    if(token == NULL){
        txNack("missing command");
        return;
    }

    if(!strcmp(token, "ping")){
        printHandler->println("pong!");
    }
    else if(!strcmp(token, "get")){
        subcommandGet(token);
    }
    else if(!strcmp(token, "set")){
        subcommandSet(token);
    }
    else if(!strcmp(token, "update")){
        subcommandUpdate(token);
    }
    else if(!strcmp(token, "reboot")){
        txAck();
        delay(1000);
        ESP.restart();
    }
    else if(!strcmp(token, "exit")){
        if(networkClient.connected()){
            networkClient.stop();
        }
    }
    else{
        txNack("invalid command");
    }
}

void ParserHandler::subcommandSet(char *token){
    int32_t tmpLong;
    
    token = strtok(NULL, " ");
    if(token == NULL){
        txNack("missing sub-arg");
        return;
    }

    if(!strcmp(token, "uartBaud")){
        // uart specific to set the baud rate
        MACRO_GET_NEXTARG("missing arg1");
        tmpLong = atol(token);
        if(tmpLong <= 0){
            txNack("0 or negative baud given");
            return;
        }

        txAck();
        // flush serial specifically before changing baud rate
        commsSerial.flush();
        commsSerial.updateBaudRate(tmpLong);
    }
    else if(!strcmp(token, "mode")){
        MACRO_GET_NEXTARG("missing arg1");

        if(!strcmp(token, "off")){
            setDisplayMode(DISPLAY_MODE_OFF);
        }
        else if(!strcmp(token, "numb")){
            setDisplayMode(DISPLAY_MODE_NUMB);
        }
        else if(!strcmp(token, "time")){
            setDisplayMode(DISPLAY_MODE_TIME);
        }
        else{
            txNack("invalid mode");
            return;
        }

        txAck();
    }
    else if(!strcmp(token, "timeFormat")){
        MACRO_GET_NEXTARG("missing arg1");

        if(!strcmp(token, "24hr")){
            timeFormat = TIME_FORMAT_24HR;
        }
        else if(!strcmp(token, "12hr")){
            timeFormat = TIME_FORMAT_12HR;
        }
        else if(!strcmp(token, "metric")){
            timeFormat = TIME_FORMAT_METRIC;
        }
        else{
            txNack("invalid mode");
            return;
        }

        txAck();
    }
    else if(!strcmp(token, "n")){
        MACRO_GET_NEXTARG("missing arg1");
        // only allow in NUMB mode
        if(dispMode != DISPLAY_MODE_NUMB){
            txNack("not in 'numb' mode");
            return;
        }

        tmpLong = atol(token);
        if(tmpLong < 0){
            txNack("number negative");
            return;
        }
        if(tmpLong > 9999){
            txNack("number too big");
            return;
        }
        displayNumber(tmpLong);
    }
    else{
        txNack("invalid sub-command");
    }
}

void ParserHandler::subcommandGet(char *token){
    token = strtok(NULL, " ");
    if(token == NULL){
        txNack("missing sub-arg");
        return;
    }

    if(!strcmp(token, "version")){
        printHandler->println(FW_VERSION);
    }
    else if(!strcmp(token, "mode")){
        switch(dispMode){
            case DISPLAY_MODE_OFF:
                printHandler->println("off");
                break;
            case DISPLAY_MODE_NUMB:
                printHandler->println("numb");
                break;
            case DISPLAY_MODE_TIME:
                printHandler->println("time");
                break;
            default:
                printHandler->println("Mode not defined");
                break;
        }
    }
    else if(!strcmp(token, "timeFormat")){
        switch(timeFormat){
            case TIME_FORMAT_24HR:
                printHandler->println("24hr");
                break;
            case TIME_FORMAT_12HR:
                printHandler->println("12hr");
                break;
            case TIME_FORMAT_METRIC:
                printHandler->println("metric");
                break;
            default:
                printHandler->println("Mode not defined");
                break;
        }
    }
    else if(!strcmp(token, "n")){
        printHandler->println(currDisplayedN);
    }
    else if(!strcmp(token, "time")){
        // copied from https://arduino.stackexchange.com/questions/52676/how-do-you-convert-a-formatted-print-statement-into-a-string-variable
        char timeStringBuff[50]; //50 chars should be enough
        strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &currTime);
        printHandler->println(timeStringBuff);
    }
    else if(!strcmp(token, "ip")){
        if(WiFi.isConnected()){
            printHandler->println(WiFi.localIP().toString().c_str());
        } else {
            printHandler->println("none");
        }
    }
    else{
        txNack("invalid sub-command");
    }
}

void ParserHandler::subcommandUpdate(char *token){
    uint32_t tmpLong;
    uint8_t stat;

    token = strtok(NULL, " ");
    if(token == NULL){
        txNack("missing sub-arg");
        return;
    }

    if(!strcmp(token, "begin")){
        token = strtok(NULL, " ");
        if(token == NULL){
            txNack("missing arg1");
            return;
        }
        tmpLong = atol(token);
        if(tmpLong == 0){
            txNack("zero update size");
            return;
        }
        stat = Update.begin(tmpLong);
        if(!stat){
            txNack("failed to init update");
            return;
        }
        txAck();
    }
    else if(!strcmp(token, "cont")){
        token = strtok(NULL, " ");
        if(token == NULL){
            txNack("missing arg1");
            return;
        }
        tmpLong = atol(token);
        if(tmpLong == 0){
            txNack("zero update size");
            return;
        }
        if(tmpLong > MAX_FW_BUFFER){
            txNack("beyond max size");
            return;
        }
        fwBufferIdx = 0;
        expectedFwBytes = tmpLong;
        txAck();
    }
    else if(!strcmp(token, "end")){
        stat = Update.end(true);
        if(stat){
            txAck();
        } else {
            printHandler->println("error when finishing firmware");
        }
    }
    else if(!strcmp(token, "cancel")){
        Update.abort();
        txAck();
        DEBUG("end: %d", expectedFwBytes);
    }
    else{
        txNack("invalid sub-command");
    }
}