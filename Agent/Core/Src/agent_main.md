# Independent receiver startup

Consume the FSBL boot-window mailbox before starting the RTOS, then initialise
the LCD, network and shared OTA receiver. Banner AGENT-LVGL-SD v4 identifies
the LVGL screen with optional SD font/theme resources. This image has no dependency
on the subsequently installed application. It never writes the installed boot area.
