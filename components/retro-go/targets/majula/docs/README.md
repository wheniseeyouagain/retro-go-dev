This is a very early "port" whose performance could be improved by adapting retro-go even further for the P4.

Networking has not been tested yet, so build the image without it:

command to build: `python rg_tool.py --target p4-game build-img --no-networking`

ESP-IDF v5.5 is recommended.

## ESP32-P4
- Status: development target

## Hardware
- Board: [ESP32-P4 Board ref: https://oshwhub.com/longxiangam/esp32p4_game](https://oshwhub.com/longxiangam/esp32p4_game) 
- st7701s 480*640 2.8 inches mipi Display 
- SD card over SDMMC (4 bits)
- NS4168 Dac (but any I2S DAC should work)
