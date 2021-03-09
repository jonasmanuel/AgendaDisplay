# E-Paper Agenda Display

Renders todays agenda on an e-paper display. The events are received through a webserver running on the esp32.
## Expected JSON Format
````
{
    "events" : [
        {
            "start" : "1615244400" // starttime in UTC seconds
            "end": "1615246200" // endtime in UTC seconds
            "title": "The event Title"
            "description": "The event description"
        },
        ...
    ]
}
````

## Libraries Used:
- [Waveshare ESP32 e-Paper Driver](https://www.waveshare.com/w/upload/5/50/E-Paper_ESP32_Driver_Board_Code.7z) for setup see [Waveshare's Wiki](https://www.waveshare.com/wiki/7.5inch_HD_e-Paper_HAT_(B))->User Guides of ESP32
- ArduinoJson by [Benoit Blanchon](http://blog.benoitblanchon.fr/) Version 6.17.3 

    An efficient and elegant JSON library for Arduino Supports JSON parsing and formatting. Uses fixed memory allocation.
- ezTime by Rop Gonggrijp Version 0.8.3

    Does NTP, datetime formatted strings, milliseconds and timezones. Drop-in replacement for Arduino Time Library See more on [Github](https://github.com/ropg/ezTime)
