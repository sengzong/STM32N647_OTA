# Receiver status for LVGL

The OTA thread is the sole publisher. The display thread takes a nonblocking
generation-checked copy; a preempted copy is discarded and retried next frame.
Only the display thread calls LVGL. No UI code runs while NOR mapping is disabled.

Metadata is accepted once the complete description is received, before the image
finishes arriving. Unknown legacy version/model fields remain unknown. Malformed
structured metadata rejects the transfer. Receipt, CRC checking, verified sector
writes, completion and failure are separate phases. Percentage is per phase.
