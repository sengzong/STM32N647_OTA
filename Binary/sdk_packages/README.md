# SDK 兼容升级包

地址、向量、配套模型和 CRC 检查已完成；这些结果不代表所有例程已上板运行。

先安装 Binary/compat 的配套 FSBL 与 Agent，再从原网页发送下面的 .ota 文件。

| 例程 | 启动时钟 | 应用与模型合并大小 | 升级文件 |
|---|---:|---:|---|
| 01_LED | 600 MHz | 3,680 B | [01_LED.ota](01_LED.ota) |
| 02_Buzzer | 600 MHz | 3,744 B | [02_Buzzer.ota](02_Buzzer.ota) |
| 03_Button | 600 MHz | 4,128 B | [03_Button.ota](03_Button.ota) |
| 04_Button_EXTI | 600 MHz | 3,744 B | [04_Button_EXTI.ota](04_Button_EXTI.ota) |
| 05_Serial | 600 MHz | 27,040 B | [05_Serial.ota](05_Serial.ota) |
| 06_IWDG | 600 MHz | 4,288 B | [06_IWDG.ota](06_IWDG.ota) |
| 07_WWDG | 600 MHz | 3,872 B | [07_WWDG.ota](07_WWDG.ota) |
| 08_Basic_Timer | 600 MHz | 5,280 B | [08_Basic_Timer.ota](08_Basic_Timer.ota) |
| 09_General_Purpose_Timer__091_Interrupt | 600 MHz | 5,728 B | [09_General_Purpose_Timer__091_Interrupt.ota](09_General_Purpose_Timer__091_Interrupt.ota) |
| 09_General_Purpose_Timer__092_PWM | 600 MHz | 7,104 B | [09_General_Purpose_Timer__092_PWM.ota](09_General_Purpose_Timer__092_PWM.ota) |
| 09_General_Purpose_Timer__093_Capture | 600 MHz | 31,520 B | [09_General_Purpose_Timer__093_Capture.ota](09_General_Purpose_Timer__093_Capture.ota) |
| 09_General_Purpose_Timer__094_Slave | 600 MHz | 30,944 B | [09_General_Purpose_Timer__094_Slave.ota](09_General_Purpose_Timer__094_Slave.ota) |
| 10_Advanced_Control_Timer__101_PWM | 600 MHz | 8,352 B | [10_Advanced_Control_Timer__101_PWM.ota](10_Advanced_Control_Timer__101_PWM.ota) |
| 10_Advanced_Control_Timer__102_Compare | 600 MHz | 7,264 B | [10_Advanced_Control_Timer__102_Compare.ota](10_Advanced_Control_Timer__102_Compare.ota) |
| 10_Advanced_Control_Timer__103_Asymmetric_PWM | 600 MHz | 8,064 B | [10_Advanced_Control_Timer__103_Asymmetric_PWM.ota](10_Advanced_Control_Timer__103_Asymmetric_PWM.ota) |
| 10_Advanced_Control_Timer__104_PWM_Input | 600 MHz | 34,176 B | [10_Advanced_Control_Timer__104_PWM_Input.ota](10_Advanced_Control_Timer__104_PWM_Input.ota) |
| 11_TPAD | 600 MHz | 5,920 B | [11_TPAD.ota](11_TPAD.ota) |
| 12_OLED | 600 MHz | 11,392 B | [12_OLED.ota](12_OLED.ota) |
| 13_MPU | 600 MHz | 30,336 B | [13_MPU.ota](13_MPU.ota) |
| 14_TFTLCD | 600 MHz | 44,928 B | [14_TFTLCD.ota](14_TFTLCD.ota) |
| 15_RGBLCD | 600 MHz | 36,864 B | [15_RGBLCD.ota](15_RGBLCD.ota) |
| 16_USMART | 600 MHz | 61,088 B | [16_USMART.ota](16_USMART.ota) |
| 17_RTC | 600 MHz | 62,368 B | [17_RTC.ota](17_RTC.ota) |
| 18_RNG | 600 MHz | 35,552 B | [18_RNG.ota](18_RNG.ota) |
| 19_DMA | 600 MHz | 53,824 B | [19_DMA.ota](19_DMA.ota) |
| 20_ADC | 600 MHz | 38,688 B | [20_ADC.ota](20_ADC.ota) |
| 21_DTS | 600 MHz | 35,552 B | [21_DTS.ota](21_DTS.ota) |
| 22_PWMDAC | 600 MHz | 42,624 B | [22_PWMDAC.ota](22_PWMDAC.ota) |
| 23_EEPROM | 600 MHz | 37,632 B | [23_EEPROM.ota](23_EEPROM.ota) |
| 24_AP3216C | 600 MHz | 37,024 B | [24_AP3216C.ota](24_AP3216C.ota) |
| 25_RS485 | 600 MHz | 48,736 B | [25_RS485.ota](25_RS485.ota) |
| 26_FDCAN__261_Internal_LoopBack | 600 MHz | 37,760 B | [26_FDCAN__261_Internal_LoopBack.ota](26_FDCAN__261_Internal_LoopBack.ota) |
| 26_FDCAN__262_Normal | 600 MHz | 37,728 B | [26_FDCAN__262_Normal.ota](26_FDCAN__262_Normal.ota) |
| 27_Touch | 600 MHz | 45,824 B | [27_Touch.ota](27_Touch.ota) |
| 28_Remote | 600 MHz | 38,272 B | [28_Remote.ota](28_Remote.ota) |
| 29_DS18B20 | 600 MHz | 35,328 B | [29_DS18B20.ota](29_DS18B20.ota) |
| 30_DHT11 | 600 MHz | 35,488 B | [30_DHT11.ota](30_DHT11.ota) |
| 31_QMI8658A | 600 MHz | 59,456 B | [31_QMI8658A.ota](31_QMI8658A.ota) |
| 32_QMC6308 | 600 MHz | 65,792 B | [32_QMC6308.ota](32_QMC6308.ota) |
| 33_Wireless | 600 MHz | 53,568 B | [33_Wireless.ota](33_Wireless.ota) |
| 34_WS2812B__341_WS2812B_DMA | 600 MHz | 20,896 B | [34_WS2812B__341_WS2812B_DMA.ota](34_WS2812B__341_WS2812B_DMA.ota) |
| 34_WS2812B__342_WS2812B_IO | 600 MHz | 13,824 B | [34_WS2812B__342_WS2812B_IO.ota](34_WS2812B__342_WS2812B_IO.ota) |
| 35_Sensor__351_OV5640 | 600 MHz | 45,056 B | [35_Sensor__351_OV5640.ota](35_Sensor__351_OV5640.ota) |
| 35_Sensor__352_IMX335 | 600 MHz | 89,216 B | [35_Sensor__352_IMX335.ota](35_Sensor__352_IMX335.ota) |
| 36_Memory_Management | 600 MHz | 38,176 B | [36_Memory_Management.ota](36_Memory_Management.ota) |
| 37_SD_NAND | 600 MHz | 58,816 B | [37_SD_NAND.ota](37_SD_NAND.ota) |
| 38_SD_Card | 600 MHz | 58,816 B | [38_SD_Card.ota](38_SD_Card.ota) |
| 39_FatFs | 600 MHz | 87,168 B | [39_FatFs.ota](39_FatFs.ota) |
| 40_Chinese_Show | 600 MHz | 76,288 B | [40_Chinese_Show.ota](40_Chinese_Show.ota) |
| 41_Picture_Show | 600 MHz | 268,864 B | [41_Picture_Show.ota](41_Picture_Show.ota) |
| 42_JPEG | 600 MHz | 341,888 B | [42_JPEG.ota](42_JPEG.ota) |
| 43_Camera | 600 MHz | 191,040 B | [43_Camera.ota](43_Camera.ota) |
| 44_Music_Player | 600 MHz | 255,488 B | [44_Music_Player.ota](44_Music_Player.ota) |
| 45_Recorder | 600 MHz | 81,184 B | [45_Recorder.ota](45_Recorder.ota) |
| 46_SPDIF | 600 MHz | 70,976 B | [46_SPDIF.ota](46_SPDIF.ota) |
| 47_Video_Player | 600 MHz | 274,144 B | [47_Video_Player.ota](47_Video_Player.ota) |
| 48_FPU__48-1_FPU_on | 600 MHz | 49,888 B | [48_FPU__48-1_FPU_on.ota](48_FPU__48-1_FPU_on.ota) |
| 48_FPU__48-2_FPU_off | 600 MHz | 42,080 B | [48_FPU__48-2_FPU_off.ota](48_FPU__48-2_FPU_off.ota) |
| 49_DSP__49-1_DSP_BasicMath | 600 MHz | 60,192 B | [49_DSP__49-1_DSP_BasicMath.ota](49_DSP__49-1_DSP_BasicMath.ota) |
| 49_DSP__49-2_DSP_FFT | 600 MHz | 88,448 B | [49_DSP__49-2_DSP_FFT.ota](49_DSP__49-2_DSP_FFT.ota) |
| 50_Handwriting_Recognition | 600 MHz | 188,416 B | [50_Handwriting_Recognition.ota](50_Handwriting_Recognition.ota) |
| 51_T9_Pinyin | 600 MHz | 170,336 B | [51_T9_Pinyin.ota](51_T9_Pinyin.ota) |
| 52_USB_Device_Card_Reader | 600 MHz | 68,544 B | [52_USB_Device_Card_Reader.ota](52_USB_Device_Card_Reader.ota) |
| 53_USB_Device_Sound_Card | 600 MHz | 75,392 B | [53_USB_Device_Sound_Card.ota](53_USB_Device_Sound_Card.ota) |
| 54_USB_Device_Virtual_Serial | 600 MHz | 56,832 B | [54_USB_Device_Virtual_Serial.ota](54_USB_Device_Virtual_Serial.ota) |
| 55_USB_Host_Disk | 600 MHz | 107,200 B | [55_USB_Host_Disk.ota](55_USB_Host_Disk.ota) |
| 56_USB_Host_HID | 600 MHz | 59,360 B | [56_USB_Host_HID.ota](56_USB_Host_HID.ota) |
| 57_ThreadX_Thread | 600 MHz | 10,208 B | [57_ThreadX_Thread.ota](57_ThreadX_Thread.ota) |
| 58_ThreadX_Semaphore | 600 MHz | 11,456 B | [58_ThreadX_Semaphore.ota](58_ThreadX_Semaphore.ota) |
| 59_ThreadX_Timer | 600 MHz | 11,456 B | [59_ThreadX_Timer.ota](59_ThreadX_Timer.ota) |
| 60_ThreadX_Queue | 600 MHz | 42,400 B | [60_ThreadX_Queue.ota](60_ThreadX_Queue.ota) |
| 61_NetXDuo_TCP_Client | 600 MHz | 80,192 B | [61_NetXDuo_TCP_Client.ota](61_NetXDuo_TCP_Client.ota) |
| 99_Applications__991_AI_People_Detection | 800 MHz | 4,065,633 B | [99_Applications__991_AI_People_Detection.ota](99_Applications__991_AI_People_Detection.ota) |
| 99_Applications__992_AI_Image_Classification | 800 MHz | 10,186,577 B | [99_Applications__992_AI_Image_Classification.ota](99_Applications__992_AI_Image_Classification.ota) |
| 99_Applications__993_AI_Instance_Segmentation | 800 MHz | 4,555,153 B | [99_Applications__993_AI_Instance_Segmentation.ota](99_Applications__993_AI_Instance_Segmentation.ota) |
| 99_Applications__994_AI_Multi_Pose_Estimation | 800 MHz | 4,391,137 B | [99_Applications__994_AI_Multi_Pose_Estimation.ota](99_Applications__994_AI_Multi_Pose_Estimation.ota) |
| 99_Applications__995_AI_Hand_Landmarks | 800 MHz | 6,506,881 B | [99_Applications__995_AI_Hand_Landmarks.ota](99_Applications__995_AI_Hand_Landmarks.ota) |
