"""Exercise the production OTA parser, slot writer and boot manager on host NOR."""
from pathlib import Path
import os
import shutil
import select
import socket
import struct
import subprocess
import sys
import threading
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import ota_protocol as protocol


def extract(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def host_firmware_tests(compat=False):
    folder = ROOT / ("Build/ota-tests-compat" if compat else "Build/ota-tests")
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "main.h").write_text("", encoding="ascii")
    (folder / "stm32_extmem_conf.h").write_text("", encoding="ascii")
    (folder / "stm32_extmem.h").write_text("", encoding="ascii")
    source = (ROOT / "Appli/Core/Src/app_netxduo.c").read_text(encoding="utf-8")
    boot = (ROOT / "FSBL/Core/Src/bootmgr.c").read_text(encoding="utf-8")
    boot = boot.replace('#include "bootmgr.h"', '#include "' + (ROOT / "FSBL/Core/Inc/bootmgr.h").as_posix() + '"')
    boot = boot.replace('#include "../../../Secure_nsclib/ota_boot_gate.h"', '#include "ota_boot_gate.h"')
    jump = extract(boot, "static void bm_jump_to(uint32_t nor_off)")
    boot = boot.replace(jump.rstrip(), "static void bm_jump_to(uint32_t off) { (void)off; longjmp(jump_target, 1); }")
    selected = "\n".join(extract(source, sig) for sig in [
        "static int ota_image_sane(", "static uint32_t ota_slot_xip(", "uint32_t ota_crc32(",
        "void ota_slot_refresh(", "int ota_store_to_next_slot(", "void ota_gateway_init(",
        "void ota_gateway_poll(", "static int ota_receive_container(",
        "static int ota_prepare_slot(", "static int ota_prepare_rollback("])
    preamble = r'''
#include <windows.h>
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_ota_flash.h"
#include "ota_boot_gate.h"
#include "ota_wire.h"
#define OTA_APP_FLASH_BASE 0x70000000u
#define OTA_APP_IMAGE_START 0x70100400u
#define OTA_APP_HEADER 0x400u
#define OTA_SECTOR 0x1000u
#define OTA_RX_BUFFER_SIZE OTA_LAYOUT_RX_CAPACITY
#define OTA_HANDOFF_ADDR 0x341B0000u
#define XSPI2 0u
#define TX_TIMER_TICKS_PER_SECOND 100u
#define NX_IP_PERIODIC_RATE 100u
#define NX_SUCCESS 0u
#define NX_NO_PACKET 1u
#define NX_NOT_CONNECTED 2u
#define NX_NULL NULL
#define EXTMEMORY_1 0u
#define EXTMEM_ENABLE 1u
#define EXTMEM_DISABLE 0u
#define EXTMEM_OK 0u
#define BM_LOG_ENABLE 0
static uint32_t clock_ticks, map_enabled, erases, inject_failure, job_calls, fail_job;
static jmp_buf jump_target;
static uint8_t ota_rx_buffer[OTA_RX_BUFFER_SIZE];
static volatile uint32_t ota_rx_len;
volatile uint8_t g_ota_receiving;
volatile uint32_t g_ota_progress;
volatile int8_t g_ota_last_slot = -1;
OtaSlotView g_slots[OTA_SLOT_COUNT];
static uint8_t ota_gateway_window;
static uint32_t ota_received_profile;
typedef int NX_TCP_SOCKET;
typedef struct packet { const uint8_t *data; ULONG length; struct packet *next; } NX_PACKET;
static NX_PACKET packet_a, packet_b;
static int packet_available, receive_gap, receive_terminal;
static ULONG tx_time_get(void) { return clock_ticks; }
static void ota_system_reset(void) { longjmp(jump_target, 2); }
static void HAL_SuspendTick(void) {}
static void __DSB(void) {}
static uint32_t __get_PRIMASK(void) { return 0; }
static void __disable_irq(void) {}
static void __set_PRIMASK(uint32_t mask) { (void)mask; }
static UINT EXTMEM_MemoryMappedMode(UINT id, UINT enable)
{
    (void)id; assert(map_enabled != enable); map_enabled=enable; return 0;
}
static UINT EXTMEM_GetMapAddress(UINT id, uint32_t *base) { (void)id; *base=0x70000000u; return 0; }
static UINT EXTMEM_EraseSector(UINT id, uint32_t off, uint32_t len)
{
    (void)id; assert(!map_enabled); assert(!(off & 4095u)); assert(!(len & 4095u));
    assert(off + len <= 0x2000000u); erases++;
    memset((void *)(uintptr_t)(0x70000000u + off), 255, len); return 0;
}
static UINT EXTMEM_Write(UINT id, uint32_t off, const uint8_t *p, uint32_t len)
{
    uint8_t *d=(uint8_t *)(uintptr_t)(0x70000000u+off); (void)id; assert(!map_enabled);
    for (uint32_t i=0;i<len;i++) { assert((d[i] & p[i]) == p[i]); d[i] &= p[i]; } return 0;
}
int ota_run_agent(const OtaJob *j)
{
    job_calls++;
    if (inject_failure || job_calls==fail_job) return -1000042;
    map_enabled=0;
    if (!(j->flags & OTA_F_WRITE_ONLY)) {
        uint32_t first=j->target & ~4095u;
        EXTMEM_EraseSector(0, first, ((j->target+j->len+4095u)&~4095u)-first);
    }
    EXTMEM_Write(0,j->target,j->data,j->len);
    assert(memcmp((void *)(uintptr_t)(0x70000000u+j->target),j->data,j->len)==0);
    map_enabled=1; return 0;
}
static UINT nx_tcp_socket_receive(NX_TCP_SOCKET *s, NX_PACKET **p, ULONG wait)
{
    (void)s;
    if (receive_gap>0) { receive_gap--; clock_ticks+=wait; return NX_NO_PACKET; }
    if (!packet_available) { clock_ticks+=wait; return receive_terminal; }
    packet_available=0; *p=&packet_a; return 0;
}
static UINT nx_packet_length_get(NX_PACKET *p, ULONG *n)
{
    *n=0; for (;p;p=p->next) *n+=p->length; return 0;
}
static UINT nx_packet_data_extract_offset(NX_PACKET *p, ULONG offset, void *out, ULONG n, ULONG *copied)
{
    uint8_t *d=out; assert(offset==0); *copied=0;
    for (;p;p=p->next) { assert(p->length<=n-*copied); memcpy(d+*copied,p->data,p->length); *copied+=p->length; }
    return 0;
}
static void nx_packet_release(NX_PACKET *p) { (void)p; }
'''
    cases = r'''
static uint8_t *flash(uint32_t off) { return (uint8_t *)(uintptr_t)(0x70000000u+off); }
int ota_bootrec_write(uint32_t state,uint32_t slot,uint32_t seq,uint32_t len)
{
    bm_mapped=map_enabled;return bm_write_rec(state,slot,seq,len);
}
uint32_t ota_bootrec_next_seq(void)
{
    BootRec rec;bm_mapped=map_enabled;
    return bm_read_rec(&rec)==0 ? rec.seq+1u : 1u;
}
static void image(uint8_t *p, uint32_t len, uint8_t seed)
{
    for(uint32_t i=0;i<len;i++) p[i]=(uint8_t)(seed+i*13u);
    uint32_t vector[2]={0x341F8000u,0x70100421u}; memcpy(p,vector,8);
}
static void reset_flash(void)
{
    memset(flash(0),255,0x2000000u); memset((void *)(uintptr_t)0x341B0000u,0,65536);
    memset((void *)(uintptr_t)0x34000000u,0,65536);
    map_enabled=0; bm_mapped=0; erases=0; inject_failure=0; g_ota_last_slot=-1; clock_ticks=0;
    job_calls=0;fail_job=0;
    ota_received_profile=0;
}
static void test_copy(void)
{
    reset_flash(); image(flash(0x400040u),13000,7);
    memset(flash(0x100000u),0,20000);
    assert(bm_copy(0x400040u,0x100400u,13000)==0);
    assert(erases==1 && map_enabled==1);
    assert(memcmp(flash(0x400040u),flash(0x100400u),13000)==0);
    assert(bm_copy(0x100400u,0x101000u,9000)==-1);
    assert(bm_copy(0x400040u,0x1ffff00u,9000)==-1);
    puts("PASS unaligned multi-sector copy and overlap/bounds rejection");
}
static void test_receive(void)
{
    uint8_t wire[300], raw[128]; uint32_t h[5]; OtaWireView v;
    image(raw,sizeof(raw),4);
    h[0]=OTA_WIRE_MAGIC;h[1]=3;h[2]=3;h[3]=sizeof(raw);h[4]=ota_crc32(raw,sizeof(raw));
    memcpy(wire,h,20);memcpy(wire+20,"cam",3);memcpy(wire+23,raw,sizeof(raw));
    for(uint32_t cut=0;cut<151;cut++) assert(ota_wire_parse(wire,cut,OTA_RX_BUFFER_SIZE,&v)==0);
    assert(ota_wire_parse(wire,151,OTA_RX_BUFFER_SIZE,&v)==1);
    assert(ota_wire_parse(wire,152,OTA_RX_BUFFER_SIZE,&v)==-1);
    packet_a=(NX_PACKET){wire,17,&packet_b}; packet_b=(NX_PACKET){wire+17,134,NULL};
    packet_available=1;receive_gap=2;receive_terminal=NX_NOT_CONNECTED;clock_ticks=0;
    assert(ota_receive_container(NULL,&v)==0 && ota_rx_len==151);
    assert(memcmp(ota_rx_buffer+23,raw,128)==0);
    wire[70]^=1;packet_available=1;
    assert(ota_receive_container(NULL,&v)==-15);wire[70]^=1;
    packet_a.length=10;packet_a.next=NULL;packet_available=1;
    assert(ota_receive_container(NULL,&v)==-11);
    packet_available=0;receive_terminal=NX_NO_PACKET;clock_ticks=0;
    assert(ota_receive_container(NULL,&v)==-10 && clock_ticks==3000);
    ((uint32_t *)wire)[2]=0xfffffff0u;
    assert(ota_wire_parse(wire,20,OTA_RX_BUFFER_SIZE,&v)==-1);
    uint32_t command[4]={OTA_CONTROL_MAGIC,OTA_COMMAND_ROLLBACK,0,0};
    assert(ota_wire_parse((uint8_t *)command,16,OTA_RX_BUFFER_SIZE,&v)==1);
    assert(v.command==OTA_COMMAND_ROLLBACK);
    uint32_t v4[6]={OTA_WIRE_MAGIC,4,3,128,ota_crc32(raw,128),2};
    memcpy(wire,v4,24);memcpy(wire+24,"cam",3);memcpy(wire+27,raw,128);
    for(uint32_t cut=0;cut<155;cut++) assert(ota_wire_parse(wire,cut,OTA_RX_BUFFER_SIZE,&v)==0);
    assert(ota_wire_parse(wire,155,OTA_RX_BUFFER_SIZE,&v)==1 && v.profile==2);
    ((uint32_t *)wire)[5]=3;
    assert(ota_wire_parse(wire,155,OTA_RX_BUFFER_SIZE,&v)==-1);
    puts("PASS packet chains, fragmented headers, idle gaps, truncation, timeout and CRC rejection");
}
static void test_continuous_upgrade(void)
{
    uint8_t expected[10001]; BootRec rec;
    reset_flash(); image(flash(0x100400u),128,1);
    uint32_t agent_vectors[2]={0x341F8000u,0x71400421u};memcpy(flash(0x1400400u),agent_vectors,8);
    for(uint32_t turn=0;turn<6;turn++)
    {
        ota_received_profile=turn%3u;
        image(expected,sizeof(expected),(uint8_t)turn);
        memcpy(ota_rx_buffer+23,expected,sizeof(expected));memcpy(ota_rx_buffer+20,"cam",3);
        int slot=ota_store_to_next_slot(ota_rx_buffer+23,sizeof(expected),(char *)ota_rx_buffer+20,3);
        assert(slot==0);
        assert(memcmp(flash(0x400040u+slot*0x400000u),expected,sizeof(expected))==0);
        assert(((OtaSlotHeader *)flash(0x400000u+slot*0x400000u))->crc32==ota_crc32(expected,sizeof(expected)));
        assert(ota_prepare_slot((uint32_t)slot)==0);
        bm_mapped=0;map_enabled=0; /* Hardware and FSBL statics reset at boot. */
        assert(BOOTMGR_Process()==0);
        assert(memcmp(flash(0x100400u),expected,sizeof(expected))==0);
        assert(bm_read_rec(&rec)==0 && rec.state==BM_ST_CONFIRMED);
        assert(BOOTMGR_GetProfile()==turn%3u);
        if (turn>0)
        {
            uint8_t previous[10001];image(previous,sizeof(previous),(uint8_t)(turn-1));
            assert(memcmp(flash(BM_BACKUP_DATA+BM_APP_HEADER),previous,sizeof(previous))==0);
        }
        /* The running image has no OTA functions or trial confirmation. */
        bm_mapped=0;map_enabled=0;
        if(setjmp(jump_target)==0) { BOOTMGR_Process(); assert(0); }
        assert(ota_gate_take()==OTA_GATE_WINDOW);
        ota_gate_write(OTA_GATE_WINDOW);ota_gateway_init();clock_ticks=999;g_ota_receiving=0;
        ota_gateway_poll(); clock_ticks=1000;g_ota_receiving=1;ota_gateway_poll();g_ota_receiving=0;
        if(setjmp(jump_target)==0) { ota_gateway_poll(); assert(0); }
        /* AXISRAM2 is clobbered by reset/ROM loading. Only SRAM1 retains tokens. */
        memset((void *)(uintptr_t)0x341B0000u,0,65536);
        assert(((volatile uint32_t *)(uintptr_t)OTA_GATE_ADDR)[0]==OTA_GATE_MAGIC);
        assert(((volatile uint32_t *)(uintptr_t)OTA_GATE_ADDR)[1]==OTA_GATE_RUN_APP);
        bm_mapped=0;map_enabled=0;
        assert(BOOTMGR_Process()==0); /* One-shot bypass, no boot loop. */
    }
    assert(ota_prepare_rollback()==0);bm_mapped=0;map_enabled=0;
    assert(BOOTMGR_Process()==0);
    image(expected,sizeof(expected),4);
    assert(memcmp(flash(0x100400u),expected,sizeof(expected))==0);
    assert(BOOTMGR_GetProfile()==1u);
    inject_failure=1;
    image(ota_rx_buffer+64,128,9);
    int failed=ota_store_to_next_slot(ota_rx_buffer+64,128,"fail",4);
    assert(failed<0);
    puts("PASS six updates reusing one staging area, latest rollback backup, standalone apps and boot gateway");
}
static void test_recovery(void)
{
    BootRec rec; uint8_t original[128];
    reset_flash();image(original,sizeof(original),1);memcpy(flash(0x100400u),original,sizeof(original));
    /* Missing agent preserves ordinary application boot. */
    assert(BOOTMGR_Process()==0);
    uint32_t agent_vectors[2]={0x341F8000u,0x71400421u};memcpy(flash(0x1400400u),agent_vectors,8);
    image(ota_rx_buffer+64,128,3);int slot=ota_store_to_next_slot(ota_rx_buffer+64,128,"trial",5);
    bm_mapped=map_enabled;assert(bm_write_rec(BM_ST_REQ_TRIAL,slot,1,128)==0);
    bm_mapped=0;map_enabled=0;assert(BOOTMGR_Process()==0);
    assert(bm_read_rec(&rec)==0 && rec.state==BM_ST_TRIAL_BOOTED);
    bm_mapped=0;map_enabled=0;
    if(setjmp(jump_target)==0) { BOOTMGR_Process();assert(0); }
    assert(ota_gate_take()==0); /* Recovery agent waits indefinitely. */
    assert(memcmp(flash(0x100400u),original,sizeof(original))==0);
    reset_flash();image(ota_rx_buffer+64,128,4);fail_job=2;
    assert(ota_store_to_next_slot(ota_rx_buffer+64,128,"cut",3)<0);
    assert(((OtaSlotHeader *)flash(0x400000u))->magic==0xffffffffu);
    ota_slot_refresh();assert(!g_slots[0].ready);
    puts("PASS legacy trial rollback, missing-agent fallback and interrupted slot commit");
}
static void test_resumed_copy(void)
{
    uint8_t original[128], next[128]; BmBackupHeader header;
    reset_flash();image(original,sizeof(original),1);image(next,sizeof(next),2);
    memcpy(flash(0x100400u),original,sizeof(original));
    memcpy(ota_rx_buffer+64,next,sizeof(next));
    assert(ota_store_to_next_slot(ota_rx_buffer+64,128,"next",4)==0);
    bm_mapped=map_enabled;assert(bm_backup_create(&header)==0);
    assert(bm_write_rec_flags(BM_ST_REQ_PERSIST,0,1,128,BM_REC_BACKUP_READY)==0);
    image(flash(0x100400u),128,9); /* Power loss left the boot copy partially replaced. */
    bm_mapped=0;map_enabled=0;
    assert(BOOTMGR_Process()==0);
    assert(memcmp(flash(0x100400u),next,sizeof(next))==0);
    assert(memcmp(flash(BM_BACKUP_DATA+BM_APP_HEADER),original,sizeof(original))==0);
    puts("PASS interrupted boot copy resumes without replacing the last good backup");
}
static void test_large_package(void)
{
#if defined(LED2_PACKAGE_COMPAT)
    const uint32_t length=0x220123u;
    uint8_t *expected=malloc(length);assert(expected);
    reset_flash();image(flash(0x100400u),128,1);
    image(expected,length,7);memcpy(expected+0xffc00u,"MODEL-WEIGHTS",13);
    uint32_t expected_crc=ota_crc32(expected,length);
    memcpy(ota_rx_buffer+64,expected,length);ota_received_profile=OTA_PROFILE_AI;
    assert(ota_store_to_next_slot(ota_rx_buffer+64,length,"AI",2)==0);
    assert(ota_prepare_slot(0)==0);bm_mapped=0;map_enabled=0;
    assert(BOOTMGR_Process()==0);
    assert(BOOTMGR_GetProfile()==OTA_PROFILE_AI);
    assert(!memcmp(flash(0x200000u),"MODEL-WEIGHTS",13));
    image(ota_rx_buffer+64,128,3);ota_received_profile=OTA_PROFILE_STANDARD;
    assert(ota_store_to_next_slot(ota_rx_buffer+64,128,"LED",3)==0);
    assert(ota_prepare_slot(0)==0);bm_mapped=0;map_enabled=0;
    assert(BOOTMGR_Process()==0 && BOOTMGR_GetProfile()==OTA_PROFILE_STANDARD);
    assert(ota_prepare_rollback()==0);bm_mapped=0;map_enabled=0;
    assert(BOOTMGR_Process()==0 && BOOTMGR_GetProfile()==OTA_PROFILE_AI);
    assert(ota_crc32(flash(0x100400u),length)==expected_crc);
    assert(!memcmp(flash(0x200000u),"MODEL-WEIGHTS",13));
    free(expected);
    puts("PASS multi-megabyte application/model install and paired rollback with clock profile");
#endif
}
int main(void)
{
    assert(VirtualAlloc((void *)(uintptr_t)0x70000000u,0x2000000u,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(VirtualAlloc((void *)(uintptr_t)0x341B0000u,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(VirtualAlloc((void *)(uintptr_t)0x34000000u,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    test_copy();test_receive();test_continuous_upgrade();test_recovery();test_resumed_copy();test_large_package();
    return 0;
}
'''
    cases = cases.replace("0x400000u+slot*0x400000u", "BM_SLOT_BASE+slot*BM_SLOT_SPACING")
    cases = cases.replace("0x400040u+slot*0x400000u", "BM_SLOT_BASE+BM_SLOT_HEADER+slot*BM_SLOT_SPACING")
    cases = cases.replace("0x400040u", "(BM_SLOT_BASE+BM_SLOT_HEADER)")
    cases = cases.replace("0x400000u", "BM_SLOT_BASE")
    cases = cases.replace("0x1400400u", "(BM_AGENT_OFFSET+BM_AGENT_HEADER)")
    cases = cases.replace("0x71400421u", "(BM_NOR_BASE+BM_AGENT_OFFSET+BM_AGENT_HEADER+0x21u)")
    fixture = folder / "firmware_host.c"
    fixture.write_text(preamble + "\n" + selected + "\n" + boot + "\n" + cases, encoding="utf-8")
    exe = folder / "firmware_host.exe"
    compiler = shutil.which("gcc")
    if not compiler:
        raise RuntimeError("Host GCC is required for the NOR emulator")
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment.get("PATH", "")
    flags = ["-DLED2_PACKAGE_COMPAT"] if compat else []
    result = subprocess.run([compiler, "-O0", "-std=c11", "-DLED2_OTA_AGENT"] + flags + ["-I" + str(folder),
                             "-I" + str(ROOT / "Appli/Core/Inc"), "-I" + str(ROOT / "Secure_nsclib"),
                             str(fixture), "-o", str(exe)], capture_output=True, text=True, env=environment)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    subprocess.run([str(exe)], check=True, env=environment)


class SenderTests(unittest.TestCase):
    def setUp(self):
        self.image = struct.pack("<II", 0x341F8000, protocol.APP_BASE + 33) + bytes(range(128))

    def test_crc_and_framing(self):
        payload, crc = protocol.make_container(self.image, "Camera")
        self.assertEqual(struct.unpack_from("<IIIII", payload),
                         (protocol.MAGIC, 3, 6, len(self.image), zlib.crc32(self.image)))
        self.assertEqual(payload[26:], self.image)

    def test_reject_wrong_target(self):
        for data in [b"", b"x" * 8, struct.pack("<II", 0x34200000, 0x71400421) + self.image,
                     struct.pack("<II", 0x34200000, protocol.APP_BASE + 32) + self.image]:
            with self.assertRaises(ValueError):
                protocol.make_container(data, "bad")

    def test_current_hex(self):
        data = protocol.load_image("appli.hex", (ROOT / "Binary/appli.hex").read_bytes())
        protocol.make_container(data, "current")
        text = (ROOT / "Binary/appli.hex").read_text()
        with self.assertRaises(ValueError):
            protocol.hex_to_bin(text.replace(":", ":01", 1))

    def test_ack_required_and_repeated_connections(self):
        payload, crc = protocol.make_container(self.image, "Camera")
        for status, reported_crc, close_early in [(0, crc, False), (-15, crc, False),
                                                   (0, crc ^ 1, False), (0, crc, True)]:
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0)); listener.listen(1)
                address = listener.getsockname()
                errors = []

                def board():
                    try:
                        with socket.create_connection(address) as connection:
                            received = bytearray()
                            while len(received) < len(payload):
                                block = connection.recv(min(7, len(payload)-len(received)))
                                if not block:
                                    break
                                received.extend(block)
                            self.assertEqual(bytes(received), payload)
                            if not close_early:
                                ack = struct.pack("<IiII", protocol.ACK_MAGIC, status, 0, reported_crc)
                                connection.sendall(ack[:3]); connection.sendall(ack[3:])
                    except BaseException as exc:
                        errors.append(exc)
                thread = threading.Thread(target=board)
                thread.start()
                connection, _ = listener.accept()
                with connection:
                    if status == 0 and reported_crc == crc and not close_early:
                        self.assertEqual(protocol.send_firmware(connection, payload, crc), 0)
                    else:
                        with self.assertRaises((ValueError, ConnectionError)):
                            protocol.send_firmware(connection, payload, crc)
                thread.join(5)
                self.assertFalse(thread.is_alive())
                self.assertEqual(errors, [])

    def test_rollback_frame(self):
        payload, crc = protocol.make_rollback()
        self.assertEqual(struct.unpack("<IIII", payload), (protocol.CONTROL_MAGIC, 1, 0, 0))
        self.assertEqual(crc, 0)

    def test_replies_while_sending_large_package(self):
        from ota_package import make_package
        image = self.image + bytes(range(256)) * 2600
        payload, crc = protocol.make_transfer("large.ota", make_package(image, 2, "AI"), "AI")
        # Success after the complete image, rejected header, forged early
        # success, and rejection after some body data are distinct outcomes.
        for reject_at, status in ((None, 0), (24, -14), (24, 0), (32768, -12)):
            with self.subTest(reject_at=reject_at, status=status):
                with socket.socket() as listener:
                    listener.bind(("127.0.0.1", 0)); listener.listen(1)
                    errors, received, progress = [], bytearray(), []

                    def board():
                        try:
                            with socket.create_connection(listener.getsockname(), timeout=5) as peer:
                                peer.settimeout(5)
                                target = len(payload) if reject_at is None else reject_at
                                while len(received) < target:
                                    chunk = peer.recv(4096)
                                    if not chunk:
                                        break
                                    received.extend(chunk)
                                if reject_at is None:
                                    self.assertEqual(bytes(received), payload)
                                    self.assertFalse(select.select([peer], [], [], 0.02)[0],
                                                     "sender must not send FIN while storage is pending")
                                else:
                                    self.assertEqual(bytes(received), payload[:len(received)])
                                ack = struct.pack("<IiII", protocol.ACK_MAGIC, status, 0, crc)
                                peer.sendall(ack[:3]); peer.sendall(ack[3:])
                                if reject_at is not None:
                                    while peer.recv(4096):
                                        pass
                        except BaseException as exc:
                            errors.append(exc)

                    thread = threading.Thread(target=board)
                    thread.start()
                    connection, _ = listener.accept()
                    with connection:
                        callback = lambda sent, total: progress.append((sent, total))
                        if reject_at is None:
                            self.assertEqual(protocol.send_firmware(connection, payload, crc, callback), 0)
                        elif status == 0:
                            with self.assertRaisesRegex(ValueError, "提前返回成功"):
                                protocol.send_firmware(connection, payload, crc, callback)
                        else:
                            with self.assertRaisesRegex(ValueError, str(status)):
                                protocol.send_firmware(connection, payload, crc, callback)
                    thread.join(6)
                    self.assertFalse(thread.is_alive())
                    self.assertEqual(errors, [])
                    self.assertEqual(progress[0][0], 24)
                    self.assertTrue(all(b[0]-a[0] <= 1024 for a,b in zip(progress,progress[1:])))
                    if reject_at == 24:
                        self.assertEqual(progress[-1][0], 24)
                    elif reject_at is not None:
                        self.assertLess(progress[-1][0], len(payload))
                    else:
                        self.assertEqual(progress[-1], (len(payload),len(payload)))


if __name__ == "__main__":
    host_firmware_tests()
    host_firmware_tests(compat=True)
    unittest.main()
