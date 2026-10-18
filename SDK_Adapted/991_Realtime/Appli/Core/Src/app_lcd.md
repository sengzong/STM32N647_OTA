# Immediate completed-frame preview

Background reload requests the just-completed camera buffer. The next capture
buffer follows a reverse rotation of three buffers, leaving both the previous
LCD front buffer and the newly requested buffer alone until vertical blank.
This removes the original DISPLAY_DELAY=1 frame delay. This assumes a camera
frame period longer than the existing LCD reload interval (about 15.46 ms).
Host tests check all relative phases at 60 FPS capture and this LCD interval;
actual refresh timing, contention and tearing need a board check.

Foreground drawing and cache publication are unchanged. The extra background
buffer remains allocated even with DISPLAY_DELAY=0 because a two-buffer swap
would immediately allow DMA to overwrite the old front before reload.
