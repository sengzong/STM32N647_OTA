"""Verify cache-safe input conversion, real queues and the performance image."""
from pathlib import Path
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    folder = ROOT / "Build/991-performance-tests"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "stm32n6xx_hal.h").write_text(r'''
#include <stdint.h>
void SCB_InvalidateDCache_by_Addr(void *, int32_t);
void SCB_CleanDCache_by_Addr(void *, int32_t);
''')
    # The same kernel is tested through portable scalar code and an intrinsic
    # shim. The target build independently verifies actual MVE instructions.
    (folder / "arm_mve.h").write_text(r'''
#include <stdint.h>
typedef struct { uint8_t lanes[16]; } uint8x16_t;
static inline uint8x16_t vdupq_n_u8(uint8_t v)
{ uint8x16_t r; for (unsigned i=0;i<16;i++) r.lanes[i]=v; return r; }
static inline uint8x16_t vld1q_u8(const uint8_t *p)
{ uint8x16_t r; for (unsigned i=0;i<16;i++) r.lanes[i]=p[i]; return r; }
static inline uint8x16_t veorq_u8(uint8x16_t a, uint8x16_t b)
{ for (unsigned i=0;i<16;i++) a.lanes[i]^=b.lanes[i]; return a; }
static inline void vst1q_u8(uint8_t *p, uint8x16_t v)
{ for (unsigned i=0;i<16;i++) p[i]=v.lanes[i]; }
''')
    source = folder / "conversion_test.c"
    source.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_input.h"
static uint8_t data[307200+64] __attribute__((aligned(32)));
static uint8_t dma_frame[307200];
static uint8_t npu_frame[307200];
static unsigned phase;
static uint32_t current_length;
void SCB_InvalidateDCache_by_Addr(void *p, int32_t length)
{
    assert(phase==0 && p==data+32 && length==(int32_t)current_length);
    memcpy(p,dma_frame,(size_t)length);
    phase=1;
}
void SCB_CleanDCache_by_Addr(void *p, int32_t length)
{
    assert(phase==1 && p==data+32 && length==(int32_t)current_length);
    for (uint32_t i=0;i<current_length;i++) assert(((uint8_t *)p)[i]==(uint8_t)(dma_frame[i]^0x80u));
    memcpy(npu_frame,p,(size_t)length);
    phase=2;
}
int main(void)
{
    const uint32_t sizes[]={0,1,15,16,17,31,32,64,256,307200};
    for (unsigned pass=0;pass<4;pass++)
    for (unsigned s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++)
    {
        current_length=sizes[s]; phase=0;
        memset(data,0x35,sizeof(data));
        memset(npu_frame,0xff,sizeof(npu_frame));
        for (uint32_t i=0;i<current_length;i++) dma_frame[i]=(uint8_t)(i+pass*29);
        app_prepare_input(data+32,current_length);
        assert(phase==2);
        for (uint32_t i=0;i<32;i++) assert(data[i]==0x35);
        for (uint32_t i=32+current_length;i<sizeof(data);i++) assert(data[i]==0x35);
        for (uint32_t i=0;i<current_length;i++) assert((int8_t)npu_frame[i]==(int)dma_frame[i]-128);
    }
    puts("PASS all pixel values, DMA invalidate -> conversion -> NPU clean, tails and guards");
    return 0;
}
''', encoding="utf-8")
    compiler = Path(r"F:/msys64/ucrt64/bin/gcc.exe")
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    include = ROOT / "SDK_Adapted/991_Performance/Appli/Core/Inc"
    for vector in (False, True):
        executable = folder / ("vector.exe" if vector else "scalar.exe")
        flags = ["-D__ARM_FEATURE_MVE=1"] if vector else []
        subprocess.run([str(compiler), "-std=c11", "-O3", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(folder), "-I" + str(include), *flags,
                        str(source), "-o", str(executable)], check=True, env=env)
        subprocess.run([str(executable)], check=True, env=env)
    subprocess.run([sys.executable, ROOT / "tools/test_991_buffers.py", "--performance"], check=True)
    output = ROOT / "Binary/991_performance"
    manifest = json.loads((output / "manifest.json").read_text())
    address = int(manifest["input_address"], 16)
    assert 0x34000000 <= address and address+921600 < 0x34200000
    assert manifest["input_bytes"] == 921600
    assert int(manifest["internal_bss_end"],16) < 0x341D0000
    assert manifest["speed_optimized"] and manifest["o3_objects"]
    print("PASS internal SRAM input placement and system-stack/heap margin")


if __name__ == "__main__":
    main()
