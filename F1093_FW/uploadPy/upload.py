"""
A python script to update the F1093 firmware with a given binary file exported from Arduino
"""
import time

import serial
import argparse
from pathlib import Path
import logging
import os


class Update:
    chunkSize = 8192

    def __init__(self, port):
        self.log = logging.getLogger('updater')
        self.ser = serial.Serial(port, 115200, timeout=1)

    def checkPing(self):
        self._writeSer('ping')
        r = self._readLine()
        if r != 'pong!':
            return False
        return True

    def setUartBaud(self, newBaud: int):
        self._writeSerAck(f'set uartBaud {newBaud:d}')
        self.ser.flush()
        self.ser.baudrate = newBaud

    def setTimeMode(self, newMode: int):
        self._writeSerAck(f'set mode {newMode:s}')

    def reset(self):
        self._writeSer('reboot')

    def update(self, file: Path):
        if not os.path.isfile(file):
            self.log.warning("file given is not a file")
            return False

        f = open(file, 'rb')
        try:
            binLen = os.path.getsize(file)

            # in case we had a previous on-going firmware update
            self._writeSerAck(f"update cancel")

            stat = self._writeSerAck(f"update begin {binLen:d}")
            if not stat:
                self.log.warning("unable to init update")
                return False

            while binLen > 0:
                toSend = min(self.chunkSize, binLen)

                stat = self._writeSerAck(f"update cont {toSend:d}")
                if not stat:
                    self.log.warning("unable to continue update")
                    return False
                # now we send the raw bytes
                self.ser.write(f.read(toSend))
                stat = self._waitForAck()
                if not stat:
                    self.log.warning("unable to continue update")
                    return False

                binLen -= toSend

            stat = self._writeSerAck(f"update end")
            if not stat:
                self.log.warning("unable to finish update")
                return False

            return True
        finally:
            f.close()

    def _writeSerAck(self, s: str):
        self._writeSer(s)
        return self._waitForAck()

    def _waitForAck(self):
        r = self._readLine()
        if r != 'ok':
            self.log.warning(f"did not return ok: {r}")
            return False
        return True

    def _writeSer(self, s: str):
        self.log.debug(f"-> {s}")
        self.ser.write(s.encode()+b'\n')

    def _readLine(self) -> str:
        r = self.ser.read_until()
        r = r.decode().strip()
        self.log.debug(f"<- {r}")
        # time.sleep(0.1)
        return r


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("file", type=Path, help="path to firmware file")
    parser.add_argument("ser", type=str, help="serial port")
    args = parser.parse_args()

    u = Update(args.ser)
    if not u.checkPing():
        print("Device did not repond with pong!")
        return
    # when updating, presumably the ISR shuts off during flash writting, causes the tubes flash a bit
    #   so better to turn the display off when updating
    u.setTimeMode('off')
    u.setUartBaud(921600)
    stat = u.update(args.file)
    if not stat:
        print("Unable to update firmware")
    else:
        u.reset()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)

    main()
