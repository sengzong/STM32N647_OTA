# Touch selection

The board selects the GT9xxx or FT5206 implementation according to the panel ID.
GT9xxx initialization failures on capacitive panels now return status 2 instead
of being silently discarded. The UI reports this status on UART. Status 0 means
normal initialization, and the existing status 1 remains resistive calibration.
