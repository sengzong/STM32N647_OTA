# Web updater

Run `python ota_gui.py`, then open `http://127.0.0.1:17800`.
The PC listens on TCP 8080 only while a task is pending. Start sending or request
rollback before resetting the board. The application need not implement OTA.

Only one task may run at a time. The UI distinguishes sending, awaiting flash
acknowledgement, confirmed completion and failure. A missing connection times out
after 180 seconds; another task can then start without restarting the web tool.
See `OTA_GUIDE.md` for the initial FSBL/Agent installation.

SDK .ota packages are accepted by both the file picker and backend make_transfer;
their application/model pairing and startup profile are preserved. Do not rename
them to .hex. If an already-running page only shows .bin/.hex, restart its Python
process after saving the new source, then refresh the browser. HTML and API
responses use Cache-Control: no-store to avoid retaining stale pages/status.

Progress counts bytes submitted by the PC. It does not confirm reception or storage.
Only the verified board acknowledgement changes the result to confirmed completion.
After changing ota_protocol.py, restart the web process so its imported sender is
updated. The board does not need reflashing for a PC sender-only change.
