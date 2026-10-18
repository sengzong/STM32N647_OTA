#ifndef __NORFLASH_MX25UM25645G_H
#define __NORFLASH_MX25UM25645G_H

#include "norflash.h"

#define NORFlash_MX25UM25645G_JEDECID_0 0xC2
#define NORFlash_MX25UM25645G_JEDECID_1 0x80
#define NORFlash_MX25UM25645G_JEDECID_2 0x39

#define NORFlash_IS_MX25UM25645G(_jedecid_) ((_jedecid_[0] == NORFlash_MX25UM25645G_JEDECID_0) && (_jedecid_[1] == NORFlash_MX25UM25645G_JEDECID_1) && (_jedecid_[2] == NORFlash_MX25UM25645G_JEDECID_2))

NORFlash_StatusTypeDef NORFlash_MX25UM25645G_Init(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t ClockInput, uint32_t *ClockRequested);

#endif
