# Camera dimensions and failure handling

After the board noise report, this experimental source is excluded from RT30 R2.
RT30 R2 compiles the original SDK app_camera.c instead. Only the withdrawn
RT60 experiment still uses this file.

Initial camera dimensions/fps follow app_imx335_mode.h. Both display and NN crop
configuration use the dimensions returned by CMW_CAMERA_Init. The realtime
overlay checks initialization failure instead of starting pipes with invalid
dimensions. Includes main.h for Error_Handler's declaration. All original pipe
formats and addresses, CSI/ISP callbacks and peripheral clocks are retained.
