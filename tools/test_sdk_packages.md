# SDK package regression

Tests absolute model addresses, erased sparse gaps, v4 clock profile/CRC parsing,
loader/receiver/overlap rejection, package truncation/corruption and sender support.
It also opens every generated SDK package and compares all original HEX segments
against the packed image, including all five AI model files. Generate the catalog
with package_sdk.py before running this test.
