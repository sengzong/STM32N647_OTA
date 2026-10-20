# Firmware display metadata

An optional UTF-8 description starts with `OTA-META1\n`, followed by four
newline-separated fields: model state (0=no, 1=yes, 2=unknown), name, version,
and the original user description. The version may be empty and is never inferred
from the protocol version, clock profile, filename or example number.

It uses the existing OFW3/OFW4 description region, so image bytes, image CRC,
boot profile and model addresses do not change. Old descriptions remain valid.
Name/version/note are bounded to 127/63/1000 UTF-8 bytes and reject controls.
This is descriptive information, not a firmware signature or authenticity check.
