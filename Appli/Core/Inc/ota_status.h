#ifndef OTA_STATUS_H
#define OTA_STATUS_H

#include <stdint.h>

typedef enum {
    OTA_UI_WAITING, OTA_UI_RECEIVING, OTA_UI_CHECKING, OTA_UI_WRITING,
    OTA_UI_READY, OTA_UI_ERROR, OTA_UI_ROLLBACK
} OtaUiPhase;

typedef struct {
    char name[128];
    char version[64];
    uint32_t profile;
    uint32_t model; /* 0=no, 1=yes, 2=not supplied */
    uint32_t metadata_ready;
    OtaUiPhase phase;
    uint32_t received, receive_total, written, write_total;
    int32_t error;
} OtaStatus;

void ota_status_begin(void);
int ota_status_package(const uint8_t *description, uint32_t length, uint32_t profile);
void ota_status_progress(OtaUiPhase phase, uint32_t done, uint32_t total);
void ota_status_phase(OtaUiPhase phase, int32_t error);
int ota_status_snapshot(OtaStatus *out);

#endif
