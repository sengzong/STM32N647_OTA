# Agent supervisor

The supervisor polls `ota_gateway_poll()` alongside the heartbeat. In a normal
boot window it returns to the application after 10 seconds without a transfer.
During receipt/programming or recovery mode it keeps the receiver available.
