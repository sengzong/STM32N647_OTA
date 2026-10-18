#ifndef __NORFLASH_BY25FQ128EL_H
#define __NORFLASH_BY25FQ128EL_H

#include "norflash.h"

#define NORFlash_BY25FQ128EL_JEDECID_0 0x68
#define NORFlash_BY25FQ128EL_JEDECID_1 0x60
#define NORFlash_BY25FQ128EL_JEDECID_2 0x18

#define NORFlash_IS_BY25FQ128EL(_jedecid_) ((_jedecid_[0] == NORFlash_BY25FQ128EL_JEDECID_0) && (_jedecid_[1] == NORFlash_BY25FQ128EL_JEDECID_1) && (_jedecid_[2] == NORFlash_BY25FQ128EL_JEDECID_2))
#define NORFlash_IS_DUAL_BY25FQ128EL(_jedecid_) ((_jedecid_[0] == NORFlash_BY25FQ128EL_JEDECID_0) && (_jedecid_[2] == NORFlash_BY25FQ128EL_JEDECID_1) && (_jedecid_[4] == NORFlash_BY25FQ128EL_JEDECID_2) && (_jedecid_[1] == NORFlash_BY25FQ128EL_JEDECID_0) && (_jedecid_[3] == NORFlash_BY25FQ128EL_JEDECID_1) && (_jedecid_[5] == NORFlash_BY25FQ128EL_JEDECID_2))

NORFlash_StatusTypeDef NORFlash_BY25FQ128EL_Init(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t ClockInput, uint32_t *ClockRequested);
NORFlash_StatusTypeDef NORFlash_DUAL_BY25FQ128EL_Init(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t ClockInput, uint32_t *ClockRequested);

#endif
