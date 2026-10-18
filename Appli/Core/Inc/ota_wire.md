# Receiver framing

`ota_wire.h` parses untrusted lengths without addition overflow. It accepts v2/v3
firmware containers and the fixed-size rollback command. Completion is determined
by declared length, never by a receive timeout. Version 3 CRC is checked by the receiver
after the complete frame has arrived. Tests compile this same header on the host.
