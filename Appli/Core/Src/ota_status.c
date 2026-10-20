/* One network writer; UI snapshots never wait or call LVGL from another thread. */
#include "ota_status.h"
#include <string.h>

static volatile uint32_t epoch;
static OtaStatus state = { .model = 2u };
#define BARRIER() __asm__ volatile ("" ::: "memory")

static void begin_write(void) { ++epoch; BARRIER(); }
static void end_write(void) { BARRIER(); ++epoch; }

static uint32_t text_copy(char *out, uint32_t capacity, const uint8_t *p, uint32_t length)
{
    uint32_t n = length < capacity - 1u ? length : capacity - 1u;
    /* Do not split the UTF-8 code point at a truncated end. */
    if (n < length) while (n && (p[n] & 0xc0u) == 0x80u) --n;
    memcpy(out, p, n); out[n] = 0;
    return n;
}

void ota_status_begin(void)
{
    begin_write();
    memset(&state, 0, sizeof(state));
    state.model = 2u;
    state.phase = OTA_UI_RECEIVING;
    end_write();
}

int ota_status_package(const uint8_t *p, uint32_t length, uint32_t profile)
{
    static const char prefix[] = "OTA-META1\n";
    OtaStatus next;
    uint32_t starts[4], sizes[4], line = 0u, start;
    if (!p || length > 2000u || profile > 2u) return -1;
    next = state;
    next.profile = profile; next.model = 2u;
    next.name[0] = 0; next.version[0] = 0;
    if (length >= sizeof(prefix)-1u && memcmp(p, prefix, sizeof(prefix)-1u) == 0)
    {
        start = sizeof(prefix)-1u;
        for (uint32_t i = start; i <= length; ++i)
        {
            if (i < length && p[i] != '\n')
            {
                if (p[i] < 32u || p[i] == 127u) return -1;
                continue;
            }
            if (line >= 4u) return -1;
            starts[line] = start; sizes[line++] = i-start; start = i+1u;
        }
        if (line != 4u || sizes[0] != 1u || p[starts[0]] < '0' || p[starts[0]] > '2' ||
            sizes[1] == 0u || sizes[1] > 127u || sizes[2] > 63u || sizes[3] > 1000u) return -1;
        next.model = p[starts[0]]-'0';
        text_copy(next.name, sizeof(next.name), p+starts[1], sizes[1]);
        text_copy(next.version, sizeof(next.version), p+starts[2], sizes[2]);
    }
    else
    {
        /* Legacy free-form descriptions contain no independent version/model. */
        text_copy(next.name, sizeof(next.name), p, length);
        for (uint32_t i = 0u; next.name[i]; ++i)
            if ((uint8_t)next.name[i] < 32u || next.name[i] == 127) next.name[i] = ' ';
    }
    next.metadata_ready = 1u;
    begin_write(); state = next; end_write();
    return 0;
}

void ota_status_progress(OtaUiPhase phase, uint32_t done, uint32_t total)
{
    begin_write(); state.phase = phase;
    if (done > total) done = total;
    if (phase == OTA_UI_WRITING) { state.written = done; state.write_total = total; }
    else { state.received = done; state.receive_total = total; }
    end_write();
}

void ota_status_phase(OtaUiPhase phase, int32_t error)
{
    begin_write(); state.phase = phase; state.error = error; end_write();
}

int ota_status_snapshot(OtaStatus *out)
{
    uint32_t before = epoch;
    if (before & 1u) return 0;
    BARRIER(); memcpy(out, &state, sizeof(*out)); BARRIER();
    return before == epoch;
}
