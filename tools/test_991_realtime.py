"""Exercise production latest-frame ownership, NPU lifecycle, sensor I/O and images."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(r"F:/stm32CubeIde/Project/Software_Package/Software_Package")
OVERLAY = ROOT / "SDK_Adapted/991_Realtime"
sys.path.insert(0, str(ROOT))
from ota_package import hex_segments, unpack_package


def function(text, name):
    start = text.index("void " + name + "(void)\n{")
    pos = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos]


def main():
    folder = ROOT / "Build/991-realtime-tests"
    folder.mkdir(parents=True, exist_ok=True)
    compiler = Path(r"F:/msys64/ucrt64/bin/gcc.exe")
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    include = OVERLAY / "Appli/Core/Inc"

    def execute(name, source, extra=(), flags=()):
        path = folder / (name + ".c")
        path.write_text(source, encoding="utf-8")
        exe = folder / (name + ".exe")
        subprocess.run([str(compiler), "-std=c11", "-O2", "-Wall", "-Wextra",
                        "-I" + str(folder), "-I" + str(include), *flags,
                        str(path), *map(str, extra), "-o", str(exe)], check=True, env=env)
        subprocess.run([str(exe)], check=True, env=env)

    (folder / "tx_api.h").write_text(r'''
#ifndef TEST_TX_H
#define TEST_TX_H
#include <stdint.h>
#include <stddef.h>
typedef struct { unsigned count; } TX_SEMAPHORE;
#define TX_WAIT_FOREVER 0xffffffffu
int tx_semaphore_create(TX_SEMAPHORE *, char *, unsigned);
int tx_semaphore_get(TX_SEMAPHORE *, unsigned);
int tx_semaphore_put(TX_SEMAPHORE *);
#endif
''')
    (folder / "stm32n6xx_hal.h").write_text(r'''
#include <stdint.h>
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
#define __DMB() ((void)0)
''')
    execute("capture", r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_capture.h"
static AppCapture q;
static uint32_t storage[3];
static uint8_t *camera, *owned;
static uint32_t mask;
static void (*race)(void);
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask=1; }
void __set_PRIMASK(uint32_t value) { mask=value; }
int tx_semaphore_create(TX_SEMAPHORE *s,char *n,unsigned v)
{ (void)n; s->count=v; return 0; }
int tx_semaphore_put(TX_SEMAPHORE *s) { assert(!s->count); ++s->count; return 0; }
int tx_semaphore_get(TX_SEMAPHORE *s,unsigned w)
{
    (void)w; assert(s->count==1); --s->count;
    if (race) { void (*f)(void)=race; race=NULL; f(); }
    return 0;
}
static void set_address(uint8_t *p)
{ assert(p && p!=owned && p!=camera); camera=p; }
static void complete(uint32_t tick)
{
    uint32_t saved=owned ? *(uint32_t *)owned : 0;
    *(uint32_t *)camera=q.frames+1;
    app_capture_complete(&q,tick,set_address);
    if (owned) assert(*(uint32_t *)owned==saved);
    assert(q.pending!=q.active && q.processing!=q.active && mask==0);
}
static void init(void)
{
    uint8_t *buffers[]={(uint8_t *)&storage[0],(uint8_t *)&storage[1],(uint8_t *)&storage[2]};
    memset(&q,0,sizeof(q)); owned=NULL; race=NULL; mask=0;
    app_capture_init(&q,buffers); camera=q.buffers[q.active];
}
static uint8_t *get(uint32_t *tick, uint32_t *seq)
{
    owned=app_capture_get(&q,tick,seq);
    assert(owned!=camera && q.pending==APP_CAPTURE_NONE && !q.ready.count);
    assert(*(uint32_t *)owned==*seq); return owned;
}
static void release(void) { app_capture_release(&q); owned=NULL; }
static void interrupt_after_token(void) { complete(30); }
static void simulate(uint64_t frame_us, uint64_t work_us)
{
    uint64_t next=frame_us, done=UINT64_MAX;
    uint32_t tick,seq,started=0,finished=0;
    init();
    while (1)
    {
        uint64_t now=next<done ? next:done;
        if (now>10000000) break;
        if (now==done) { finished++; release(); done=UINT64_MAX; }
        if (now==next) { complete((uint32_t)(now/1000)); next+=frame_us; }
        if (!owned && q.ready.count)
        {
            get(&tick,&seq); started++;
            assert((uint32_t)(now/1000)-tick <= (frame_us+999)/1000);
            done=now+work_us;
        }
    }
    double fps=finished/10.0;
    double bound=1000000.0/(frame_us>work_us ? frame_us:work_us);
    assert(fps>=bound-0.3 && fps<=bound+0.1);
    printf("PASS capture %.1f FPS / inference work %.1f ms: %.1f completed FPS, %u obsolete frames replaced\n",
           1000000.0/frame_us,work_us/1000.0,fps,q.replaced);
    assert(started==finished || started==finished+1);
}
int main(void)
{
    uint32_t tick,seq;
    init();
    for (unsigned i=0;i<100;i++) complete(i);
    assert(q.ready.count==1 && q.replaced==99);
    get(&tick,&seq); assert(seq==100 && tick==99);
    for (unsigned i=100;i<200;i++) complete(i);
    release(); get(&tick,&seq); assert(seq==200); release();
    complete(20); race=interrupt_after_token; get(&tick,&seq); assert(tick==30); release();
    init(); q.frames=0xfffffffeu;
    complete(0xfffffff0u); complete(5); get(&tick,&seq);
    assert(seq==0 && tick==5); release();
    simulate(33333,33000); simulate(16667,33000); simulate(16667,25000); simulate(16667,80000);
    puts("PASS newest frame, ISR/consumer race, saturation, wrap and inference-buffer protection");
}
''')
    (folder / "ll_aton_runtime.h").write_text(r'''
typedef struct { unsigned runs,step; } NN_Instance_TypeDef;
typedef enum { LL_ATON_RT_NO_WFE,LL_ATON_RT_WFE,LL_ATON_RT_DONE } LL_ATON_RT_RetValues_t;
void LL_ATON_RT_RuntimeInit(void);
void LL_ATON_RT_Init_Network(NN_Instance_TypeDef *);
void LL_ATON_RT_Reset_Network(NN_Instance_TypeDef *);
LL_ATON_RT_RetValues_t LL_ATON_RT_RunEpochBlock(NN_Instance_TypeDef *);
void wait_event(void);
#define LL_ATON_OSAL_WFE() wait_event()
''')
    execute("nn", r'''
#include <assert.h>
#include <stdio.h>
#include "app_nn.h"
static unsigned runtimes,networks,waits;
void LL_ATON_RT_RuntimeInit(void) { assert(++runtimes==1); }
void LL_ATON_RT_Init_Network(NN_Instance_TypeDef *i) { i->step=0; assert(++networks==1); }
void LL_ATON_RT_Reset_Network(NN_Instance_TypeDef *i) { i->runs++; i->step=0; }
void wait_event(void) { waits++; }
LL_ATON_RT_RetValues_t LL_ATON_RT_RunEpochBlock(NN_Instance_TypeDef *i)
{ return ++i->step==3 ? LL_ATON_RT_DONE : i->step==2 ? LL_ATON_RT_WFE:LL_ATON_RT_NO_WFE; }
int main(void)
{
    NN_Instance_TypeDef i={0}; app_nn_init(&i);
    for (unsigned n=0;n<100;n++) { app_nn_run(&i); assert(i.runs==n+1 && i.step==3); }
    assert(waits==100 && networks==1 && runtimes==1);
    puts("PASS one runtime/network initialization, 100 resets/inferences and asynchronous waits");
}
''')
    sensor = SDK / "Middlewares/STM32_MW_CAMERA/sensors/imx335"
    (folder / "cmsis_compiler.h").write_text("#include <stdint.h>\n")
    execute("sensor", r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "imx335.h"
#include "app_imx335_mode.h"
static uint8_t regs[65536];
static int fail=-1;
static int32_t ok(void) { return 0; }
static int32_t tick(void) { static int32_t t; return t++; }
static int32_t wr(uint16_t a,uint16_t r,uint8_t *p,uint16_t n)
{ (void)a; if ((int)r==fail) return -1; memcpy(regs+r,p,n); return 0; }
static int32_t rd(uint16_t a,uint16_t r,uint8_t *p,uint16_t n)
{ (void)a; if ((int)r==fail) return -1; memcpy(p,regs+r,n); return 0; }
static unsigned value(unsigned r,unsigned n)
{ unsigned v=0; for (unsigned i=0;i<n;i++) v|=(unsigned)regs[r+i]<<(8*i); return v; }
static void setup(IMX335_Object_t *p)
{
    memset(p,0,sizeof(*p)); memset(regs,0,sizeof(regs));
    regs[0x3030]=0x94; regs[0x3031]=0x11; regs[0x3034]=0x26; regs[0x3035]=2;
    IMX335_IO_t io={ok,ok,0x1a,wr,rd,tick}; assert(IMX335_RegisterBusIO(p,&io)==0);
}
int main(void)
{
    IMX335_Object_t p;
    setup(&p); assert(IMX335_Init(&p,IMX335_R2592_1944,10)==0);
    assert(p.IsInitialized && !app_imx335_is_binned() && value(0x3034,2)==550);
    assert(IMX335_SetExposure(&p,30000)==0);
    setup(&p); assert(IMX335_Init(&p,APP_IMX335_R1296_972,10)==0);
    assert(p.IsInitialized && app_imx335_is_binned());
    assert(value(0x3034,2)==275 && value(0x3030,3)==4500);
    assert(value(0x3056,2)==984 && regs[0x3199]==0x30 && regs[0x3300]==1);
    assert(IMX335_SetFrequency(&p,IMX335_INCK_37MHZ)==0 && value(0x3034,2)==275);
    assert(IMX335_SetExposure(&p,16600)==0 && value(0x3058,3)>=17 && !regs[0x3001]);
    assert(IMX335_SetExposure(&p,-1)==-1 && IMX335_SetExposure(&p,17000)==-1);
    assert(IMX335_MirrorFlipConfig(&p,IMX335_FLIP)==0);
    assert(value(0x3074,2)==0x1008 && regs[0x304f]==1 && regs[0x3080]==0xfc && regs[0x30ad]==0x78);
    assert(IMX335_MirrorFlipConfig(&p,IMX335_MIRROR_FLIP_NONE)==0);
    assert(value(0x3074,2)==0x00a8 && regs[0x304f]==0 && regs[0x3080]==4 && regs[0x30ad]==8);
    setup(&p); assert(IMX335_MirrorFlipConfig(&p,IMX335_FLIP)==0);
    assert(IMX335_Init(&p,APP_IMX335_R1296_972,10)==0 && regs[0x3080]==0xfc && value(0x3074,2)==0x1008);
    fail=0x3018; setup(&p); assert(IMX335_Init(&p,APP_IMX335_R1296_972,10)==-1 && !p.IsInitialized);
    fail=0x3199; setup(&p); assert(IMX335_Init(&p,APP_IMX335_R1296_972,10)==-1 && !p.IsInitialized);
    fail=-1; setup(&p); assert(IMX335_Init(&p,999,10)==-1 && !p.IsInitialized);
    puts("PASS real IMX335 driver: normal/binning modes, clock setup, exposure limits and I2C failures");
}
''', extra=[OVERLAY / "Middlewares/STM32_MW_CAMERA/sensors/imx335/imx335.c", sensor / "imx335_reg.c"],
            flags=["-I" + str(sensor)])

    lcd = (OVERLAY / "Appli/Core/Src/app_lcd.c").read_text(encoding="utf-8")
    execute("lcd", r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define DISPLAY_BUFFER_NB 3
#define LTDC_RELOAD_VERTICAL_BLANKING 1
static uint8_t app_lcd_bg_buffer[3][64];
static unsigned app_lcd_bg_buffer_disp_idx,app_lcd_bg_buffer_fill_idx;
static unsigned requested;
static void *bsp_lcd_get_ltdc_handle(void) { return NULL; }
static void HAL_LTDC_SetAddress_NoReload(void *h,uint32_t addr,unsigned layer)
{
    (void)h; assert(!layer);
    for (requested=0;requested<3;requested++) if ((uint32_t)(uintptr_t)app_lcd_bg_buffer[requested]==addr) return;
    assert(0);
}
static void HAL_LTDC_ReloadLayer(void *h,unsigned mode,unsigned layer)
{ (void)h; assert(mode==1 && !layer); }
''' + function(lcd,"app_lcd_switch_bg_buffer") + r'''
int main(void)
{
    for (unsigned phase=0;phase<15460;phase+=101)
    {
        unsigned front=1;
        uint64_t next_blank=phase;
        app_lcd_bg_buffer_fill_idx=0; app_lcd_bg_buffer_disp_idx=1; requested=1;
        for (uint64_t frame=16667;frame<10000000;frame+=16667)
        {
            while (next_blank<=frame) { front=requested; next_blank+=15460; }
            unsigned completed=app_lcd_bg_buffer_fill_idx;
            app_lcd_switch_bg_buffer();
            assert(requested==completed);
            assert(app_lcd_bg_buffer_fill_idx!=front && app_lcd_bg_buffer_fill_idx!=requested);
        }
    }
    puts("PASS real LCD switch: latest image and three-buffer separation across vblank phases");
}
''',flags=["-Wno-pointer-to-int-cast"])

    project = SDK / "Projects/99_Applications/991_AI_People_Detection"
    model = project / "Binary/network-data.hex"
    for variant, fps in (("30",30),("60_trial",60)):
        output = ROOT / ("Binary/991_realtime_" + variant)
        manifest = json.loads((output / "manifest.json").read_text())
        for name,digest in manifest["files"].items():
            assert hashlib.sha256((output/name).read_bytes()).hexdigest()==digest
        image,profile,label = unpack_package((output/("991_realtime_"+variant+".ota")).read_bytes())
        assert profile==2 and label==manifest["name"]
        app = (output/"appli.hex").read_text()
        paired = model.read_text()
        assert (output/("991_realtime_"+variant+".hex")).read_bytes()==(output/"appli.hex").read_bytes()
        assert (output/"network-data.hex").read_bytes()==model.read_bytes()
        assert hex_segments((output/"application_and_network.hex").read_text())==hex_segments(app)+hex_segments(paired)
        for address,data in hex_segments(app)+hex_segments(paired):
            assert image[address-0x70100400:address-0x70100400+len(data)]==data
        assert manifest["capture_target_fps"]==fps and manifest["persistent_runtime"] and manifest["latest_frame"]
        assert int(manifest["internal_bss_end"],16)<0x341D0000
        commands=json.loads((ROOT/("Build/991-realtime-"+variant)/"commands.json").read_text())
        overridden=[c for c in commands if str(OVERLAY) in c["source"]]
        assert len(overridden)==(5 if fps==60 else 2), [c["source"] for c in overridden]
        if fps==30:
            camera_sources=[project/"Appli/Core/Src/app_camera.c",
                            SDK/"Middlewares/STM32_MW_CAMERA/sensors/cmw_imx335.c",
                            SDK/"Middlewares/STM32_MW_CAMERA/sensors/imx335/imx335.c"]
            baseline=json.loads((ROOT/"Build/991-performance/commands.json").read_text())
            for camera_source in camera_sources:
                actual=next(c for c in commands if Path(c["original"])==camera_source)
                original=next(c for c in baseline if Path(c["original"])==camera_source)
                assert Path(actual["source"])==camera_source
                assert hashlib.sha256(camera_source.read_bytes()).hexdigest()==actual["sha256"]
                assert Path(actual["object"]).read_bytes()==Path(original["object"]).read_bytes(), camera_source
            assert manifest["camera_pipeline"]=="original_sdk"
            assert len(manifest["source_overrides"])==2
        assert all(("-DAPP_CAPTURE_BIN60=1" in c["command"])==(fps==60) for c in commands)
    assert (include/"app_input.h").read_bytes()==(ROOT/"SDK_Adapted/991_Performance/Appli/Core/Inc/app_input.h").read_bytes()
    print("PASS target builds, SRAM bounds, model pairing, OTA CRCs/hashes; RT30 camera objects match working performance build")


if __name__ == "__main__":
    main()
