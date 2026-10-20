# Portable executable integration tests

Build the tool first, then run `python tools/test_install_merger.py` on Windows.
Tests execute the actual C# EXE and inspect its output with independent Python HEX,
CRC32 and SHA256 readers. The current three components must produce exactly the
published initial_install.hex bytes. Tests cover clock profiles, sparse model gaps,
header lengths/CRCs, initialized sectors, legacy Agent and combined-input rejection,
damaged FSBL signatures, invalid HEX/vectors/ranges, model/application separation,
overwrite preservation and input-path protection.

A detached copy runs from a folder containing spaces and Chinese characters with
only Windows System32 in PATH, demonstrating it needs no Python installation.
The actual Windows Forms page is rendered offscreen to Build/install-merger-tests/preview.png.
All generated fixtures stay under Build/install-merger-tests; no board access occurs.
