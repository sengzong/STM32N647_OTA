# Installation UI regression

Compiles the production model_select.c with simulated LCD/touch and OTA APIs.
Checks automatic selection of the sole valid image, two separate confirmation
actions, disabled empty entries, button redraw, Back, receiving guards, and
out-of-screen touch rejection. Run with host GCC in PATH:
`python tools/test_model_select.py`.
