# Independent receiver startup

Consume the FSBL boot-window mailbox before starting the RTOS, then initialise
the network and shared OTA receiver. Banner version 2 identifies the startup-window
implementation. This image has no dependency on the subsequently installed application.
