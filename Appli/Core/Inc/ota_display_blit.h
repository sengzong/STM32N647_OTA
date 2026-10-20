#ifndef OTA_DISPLAY_BLIT_H
#define OTA_DISPLAY_BLIT_H

#include <stdint.h>

/* Same portrait transform as the board's RGBLCD drawing primitives. */
static inline void ota_display_blit(uint16_t *frame, uint32_t pw, uint32_t ph,
        uint32_t landscape, int x1, int y1, int x2, int y2, const uint16_t *pixels)
{
    int width = (int)(landscape ? pw : ph), height = (int)(landscape ? ph : pw);
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x, ++pixels)
            if (x >= 0 && x < width && y >= 0 && y < height)
                frame[landscape ? (uint32_t)y*pw+(uint32_t)x :
                      (ph-1u-(uint32_t)x)*pw+(uint32_t)y] = *pixels;
}

#endif
