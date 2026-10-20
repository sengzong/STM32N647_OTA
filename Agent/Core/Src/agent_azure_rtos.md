# Agent supervisor

The supervisor polls `ota_gateway_poll()` alongside the heartbeat. In a normal
boot window it returns to the application after 10 seconds without a transfer.
During receipt/programming or recovery mode it keeps the receiver available.
The supervisor also owns LVGL and polls the OTA screen. Its stack is 8 KB.
The network thread only publishes bounded status and never calls LVGL itself.
