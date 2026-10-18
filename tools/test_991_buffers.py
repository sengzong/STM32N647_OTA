"""Run the SDK queue and modified application's actual capture callback on host."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK_PROJECT = Path(r"F:/stm32CubeIde/Project/Software_Package/Software_Package/Projects/99_Applications/991_AI_People_Detection")
OVERLAY = ROOT / "SDK_Adapted/991_AI_People_Detection"
sys.path.insert(0, str(ROOT))
from ota_package import hex_segments, unpack_package
from ota_protocol import make_transfer


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--performance", action="store_true")
    args = parser.parse_args()
    overlay = ROOT / "SDK_Adapted/991_Performance" if args.performance else OVERLAY
    folder = ROOT / ("Build/991-performance-queue-tests" if args.performance else "Build/991-buffer-tests")
    folder.mkdir(parents=True, exist_ok=True)
    for name in ("stm32n6xx_hal.h", "postprocess_conf.h"):
        (folder / name).write_text("/* Host peripheral definitions are not needed. */\n")
    (folder / "tx_api.h").write_text(r'''
#ifndef TX_API_H
#define TX_API_H
#include <stddef.h>
#include <stdint.h>
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef struct { ULONG count; } TX_SEMAPHORE;
#define TX_NO_WAIT 0u
#define TX_WAIT_FOREVER (~0ul)
#define TX_NO_INSTANCE 1u
UINT tx_semaphore_create(TX_SEMAPHORE *, char *, ULONG);
UINT tx_semaphore_get(TX_SEMAPHORE *, ULONG);
UINT tx_semaphore_put(TX_SEMAPHORE *);
#endif
''')
    shutil.copy2(SDK_PROJECT / "Appli/Core/Inc/app_bqueue.h", folder / "app_bqueue.h")
    shutil.copy2(overlay / "Appli/Core/Inc/app_config.h", folder / "app_config.h")
    application = (overlay / "Appli/Core/Src/app.c").read_text(encoding="utf-8")
    start = application.index("static void app_camera_nn_pipe_frame_cb(void)\n{")
    opening = application.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (application[end] == "{") - (application[end] == "}")
        end += 1
    callback = application[start:end]
    preamble = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_bqueue.h"
_Static_assert(NN_INPUT_BUFFER_COUNT == 3, "production input count");
_Static_assert(BQUEUE_MAX_BUFFERS >= NN_INPUT_BUFFER_COUNT, "queue capacity");
static app_bqueue_t nn_input_queue;
static uint8_t *camera;
static uint8_t *processing;
UINT tx_semaphore_create(TX_SEMAPHORE *s, char *name, ULONG value)
{ (void)name; s->count = value; return 0; }
UINT tx_semaphore_get(TX_SEMAPHORE *s, ULONG wait)
{
    if (!s->count) { assert(wait == TX_NO_WAIT); return TX_NO_INSTANCE; }
    s->count--; return 0;
}
UINT tx_semaphore_put(TX_SEMAPHORE *s) { s->count++; return 0; }
static void app_camera_nn_pipe_set_address(uint8_t *next)
{ assert(next && next != processing && next != camera); camera = next; }
'''
    cases = r'''
typedef struct { double fps; unsigned dropped; unsigned started; } Result;
static Result simulate(unsigned count, uint64_t work_us)
{
    uint8_t storage[3][64];
    uint8_t *pointers[3] = {storage[0], storage[1], storage[2]};
    uint64_t next_frame = 33333, done = UINT64_MAX;
    uint64_t first_start = 0, last_start = 0;
    unsigned measured = 0;
    Result result = {0};
    memset(&nn_input_queue, 0, sizeof(nn_input_queue));
    processing = NULL; camera = NULL;
    app_bqueue_init(&nn_input_queue, (uint8_t)count, pointers);
    camera = app_bqueue_get_free(&nn_input_queue, 0);
    for (;;)
    {
        uint64_t now = next_frame < done ? next_frame : done;
        if (now > 10000000) break;
        if (now == done)
        {
            app_bqueue_put_free(&nn_input_queue);
            processing = NULL;
            done = UINT64_MAX;
        }
        if (now == next_frame)
        {
            ULONG before = nn_input_queue.ready.count;
            assert(camera && camera != processing);
            camera[0] = (uint8_t)(now / 33333);
            app_camera_nn_pipe_frame_cb();
            if (before == nn_input_queue.ready.count) result.dropped++;
            next_frame += 33333;
        }
        if (!processing && nn_input_queue.ready.count)
        {
            processing = app_bqueue_get_ready(&nn_input_queue);
            assert(processing && processing != camera);
            done = now + work_us;
            result.started++;
            if (now >= 1000000)
            {
                if (!measured) first_start = now;
                last_start = now;
                measured++;
            }
        }
        assert(nn_input_queue.free.count <= count);
        assert(nn_input_queue.ready.count < count);
        assert(nn_input_queue.free.count + nn_input_queue.ready.count +
               (processing ? 1 : 0) + 1 == count);
    }
    assert(measured > 1);
    result.fps = (measured - 1) * 1000000.0 / (last_start - first_start);
    return result;
}
int main(void)
{
    Result two = simulate(2, 42000);
    Result three = simulate(NN_INPUT_BUFFER_COUNT, 42000);
    assert(two.fps > 14.9 && two.fps < 15.1);
    assert(three.fps > 23.7 && three.fps < 23.9);
    assert(three.dropped < two.dropped);
    printf("PASS actual callback/queue, 30 FPS input and 42 ms work: %.3f -> %.3f FPS\n",
           two.fps, three.fps);
    assert(simulate(2, 10000).fps > 29.9);
    assert(simulate(NN_INPUT_BUFFER_COUNT, 10000).fps > 29.9);
    assert(simulate(NN_INPUT_BUFFER_COUNT, 100000).fps > 9.9);
    puts("PASS buffer ownership, wraparound, saturation and recovery across processing times");

    app_bqueue_t output;
    uint8_t a, b;
    uint8_t *pointers[2] = {&a, &b};
    app_bqueue_init(&output, 2, pointers);
    assert(app_bqueue_get_free(&output, 0) == &a);
    assert(app_bqueue_get_free(&output, 0) == &b);
    assert(app_bqueue_get_free(&output, 0) == NULL);
    app_bqueue_put_ready(&output); app_bqueue_put_ready(&output);
    assert(app_bqueue_get_ready(&output) == &a); app_bqueue_put_free(&output);
    assert(app_bqueue_get_ready(&output) == &b); app_bqueue_put_free(&output);
    assert(output.free.count == 2 && output.ready.count == 0);
    puts("PASS two-buffer output queue fits the enlarged queue structure");
    return 0;
}
'''
    harness = folder / "capture_test.c"
    harness.write_text(preamble + callback + cases, encoding="utf-8")
    compiler = Path(r"F:/msys64/ucrt64/bin/gcc.exe")
    executable = folder / "capture_test.exe"
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    subprocess.run([str(compiler), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(folder), str(harness),
                    str(SDK_PROJECT / "Appli/Core/Src/app_bqueue.c"), "-o", str(executable)],
                   check=True, env=env)
    subprocess.run([str(executable)], check=True, env=env)
    manifest_path = ROOT / ("Binary/991_performance/manifest.json" if args.performance else "Binary/991_triple_buffer/manifest.json")
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        assert manifest["input_buffers"] == 3
        assert manifest["extra_psram_bytes"] == (0 if args.performance else 307200)
        assert manifest["extram_bytes"] <= 16 * 1024 * 1024
        for name, expected in manifest["files"].items():
            assert hashlib.sha256((manifest_path.parent / name).read_bytes()).hexdigest() == expected
        package_name = "991_performance.ota" if args.performance else "991_triple_buffer.ota"
        package = (manifest_path.parent / package_name).read_bytes()
        image, profile, description = unpack_package(package)
        assert profile == 2 and description == ("991 speed SRAM" if args.performance else "991 triple buffer")
        for filename in ("appli.hex", "network-data.hex"):
            for address, block in hex_segments((manifest_path.parent / filename).read_text()):
                offset = address - 0x70100400
                assert image[offset:offset + len(block)] == block
        assert (manifest_path.parent / "network-data.hex").read_bytes() == (SDK_PROJECT / "Binary/network-data.hex").read_bytes()
        renamed, _ = make_transfer("991_triple_buffer.ota", package, "991 three buffers")
        assert unpack_package(renamed) == (image, 2, "991 three buffers")
        print("PASS target image input-buffer size and external RAM bounds")
        print("PASS package CRC, output hashes, unchanged model and sender compatibility")


if __name__ == "__main__":
    main()
