# Command-line updater

`python ota_send.py --file Binary/appli.bin --desc "Camera v2" --yes`

`python ota_send.py --rollback --yes`

Start the command first, then reset the board. Framing, validation and acknowledgement
handling are shared with the web updater in `ota_protocol.py`; raw flashing is removed.
Exit status is nonzero when the board rejects the image or fails to acknowledge it.
