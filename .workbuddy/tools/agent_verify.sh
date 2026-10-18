#!/bin/bash
# Clean out-of-tree verification build of the resident OTA AGENT project.
#
# Mirrors exactly what Agent/.cproject configures (Release, -Os, -mcmse,
# -DLED2_OTA_AGENT) and links with Agent/STM32N647X0HXQ_ROMxspi2_RAMxspi1_agent.ld
# so the resulting .bin can be checked against what the FSBL requires:
#   .isr_vector at 0x71400400, SP inside 0x34000000..0x34200000,
#   Reset_Handler inside the XIP window.
#
# Usage:  bash agent_verify.sh

ROOT="F:/stm32CubeIde/Project/LED_2_noai"
TC="/f/stm32CubeIde/STM32CubeIDE_1.17.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.12.3.rel1.win32_1.1.0.202410251130/tools/bin"
OUT="C:/Users/ls/AppData/Local/Temp/agent_verify"

export PATH="$TC:$PATH"
rm -rf "$OUT"
mkdir -p "$OUT/obj"
cd "$OUT" || exit 1

echo "toolchain: $(arm-none-eabi-gcc --version | head -1)"

INC="-I$ROOT/Appli/Core/Inc \
-I$ROOT/Secure_nsclib \
-I$ROOT/Drivers/STM32N6xx_HAL_Driver/Inc \
-I$ROOT/Drivers/CMSIS/Device/ST/STM32N6xx/Include \
-I$ROOT/Drivers/STM32N6xx_HAL_Driver/Inc/Legacy \
-I$ROOT/Drivers/CMSIS/Include \
-I$ROOT/Appli/Drivers/BSP/UART \
-I$ROOT/Appli/Drivers/BSP/YT8512C \
-I$ROOT/Middlewares/THREADX/common/inc \
-I$ROOT/Middlewares/THREADX/ports/cortex_m55/gnu/inc \
-I$ROOT/Middlewares/NETXDUO/common/inc \
-I$ROOT/Middlewares/NETXDUO/addons/dhcp \
-I$ROOT/Middlewares/NETXDUO/common/drivers/ethernet \
-I$ROOT/Middlewares/NETXDUO/ports/cortex_m55/gnu/inc \
-I$ROOT/Middlewares/NETXDUO/tsn/inc"

DEF="-DUSE_HAL_DRIVER -DSTM32N647xx -DTX_SINGLE_MODE_SECURE -DTX_INCLUDE_USER_DEFINE_FILE -DNX_INCLUDE_USER_DEFINE_FILE -DLED2_OTA_AGENT"

CF="-mcpu=cortex-m55 -std=gnu11 -Os $DEF -ffunction-sections -fdata-sections -Wall -mcmse --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb"
AF="-mcpu=cortex-m55 $DEF -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb"

# ---- application sources reused verbatim ------------------------------------
SRCS="
$ROOT/Agent/Core/Src/agent_main.c
$ROOT/Agent/Core/Src/agent_azure_rtos.c
$ROOT/Appli/Core/Src/app_netxduo.c
$ROOT/Appli/Core/Src/nx_stm32_phy_driver.c
$ROOT/Appli/Core/Src/secure_nsc.c
$ROOT/Appli/Core/Src/stm32n6xx_hal_msp.c
$ROOT/Appli/Core/Src/stm32n6xx_it.c
$ROOT/Appli/Core/Src/syscalls.c
$ROOT/Appli/Core/Src/sysmem.c
$ROOT/Appli/Core/Src/system_stm32n6xx_s.c
$ROOT/Appli/Drivers/BSP/UART/uart.c
$ROOT/Appli/Drivers/BSP/YT8512C/yt8512c.c
"

# ---- HAL: same 24 modules as Appli/.project ---------------------------------
HAL_LIST="stm32n6xx_hal stm32n6xx_hal_cortex stm32n6xx_hal_dcmipp stm32n6xx_hal_dma \
stm32n6xx_hal_dma2d stm32n6xx_hal_dma_ex stm32n6xx_hal_eth stm32n6xx_hal_eth_ex \
stm32n6xx_hal_exti stm32n6xx_hal_gpio stm32n6xx_hal_i2c stm32n6xx_hal_i2c_ex \
stm32n6xx_hal_ltdc stm32n6xx_hal_ltdc_ex stm32n6xx_hal_pwr stm32n6xx_hal_pwr_ex \
stm32n6xx_hal_rcc stm32n6xx_hal_rcc_ex stm32n6xx_hal_rif stm32n6xx_hal_tim \
stm32n6xx_hal_tim_ex stm32n6xx_hal_uart stm32n6xx_hal_uart_ex stm32n6xx_hal_xspi"
for h in $HAL_LIST; do SRCS="$SRCS
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/$h.c"; done

# ---- middleware directories compiled wholesale ------------------------------
# ThreadX's cortex-M55 port is mostly ASSEMBLY (tx_thread_schedule.S,
# tx_thread_stack_build.S, tx_timer_interrupt.S, ...). Forgetting the .S files
# links with "undefined reference to _tx_thread_schedule" - and no .c file
# provides those. So every directory is globbed for both extensions.
ASMS=""
for d in \
    "$ROOT/Middlewares/NETXDUO/common/src" \
    "$ROOT/Middlewares/NETXDUO/addons/dhcp" \
    "$ROOT/Middlewares/NETXDUO/common/drivers/ethernet" \
    "$ROOT/Middlewares/NETXDUO/tsn/src" \
    "$ROOT/Middlewares/THREADX/common/src" \
    "$ROOT/Middlewares/THREADX/ports/cortex_m55/gnu/src"
do
    [ -d "$d" ] || { echo "note: no such dir $d"; continue; }
    for f in "$d"/*.c; do [ -f "$f" ] && SRCS="$SRCS
$f"; done
    for f in "$d"/*.S; do [ -f "$f" ] && ASMS="$ASMS
$f"; done
done

n=$(echo "$SRCS" | grep -c '\.c$')
m=$(echo "$ASMS" | grep -c '\.S$')
echo "sources: $n C + $m asm + startup/low-level"

fail=0
objs=()
i=0
for src in $SRCS; do
    [ -n "$src" ] || continue
    i=$((i + 1))
    obj="$OUT/obj/o$i.o"
    if ! arm-none-eabi-gcc "$src" $CF $INC -c -o "$obj" 2> "$OUT/obj/o$i.err"; then
        echo "CC FAILED: ${src#$ROOT/}"
        cat "$OUT/obj/o$i.err"
        fail=$((fail + 1))
    else
        if [ -s "$OUT/obj/o$i.err" ]; then
            echo "--- warnings: ${src#$ROOT/}"
            cat "$OUT/obj/o$i.err"
        fi
        objs+=("$obj")
    fi
done

# ---- startup + ThreadX low level + ThreadX cortex-M55 port (asm-with-cpp) ---
# -x assembler-with-cpp MUST come before the input file, otherwise gcc warns
# "'-x assembler-with-cpp' after last input file has no effect" and treats a
# lowercase .s file as raw assembly (no preprocessing) - which is not what
# CubeIDE's generated makefile does.
for asm in \
    "$ROOT/Appli/Core/Startup/startup_stm32n647x0hxq.s" \
    "$ROOT/Appli/Core/Src/tx_initialize_low_level.S" \
    $ASMS
do
    [ -n "$asm" ] || continue
    i=$((i + 1))
    tag=$(echo "${asm#$ROOT/}" | sed 's|/|_|g')
    if ! arm-none-eabi-gcc -x assembler-with-cpp "$asm" $AF $INC -c -o "$OUT/obj/$tag.o" 2> "$OUT/obj/$tag.err"; then
        echo "AS FAILED: ${asm#$ROOT/}"; cat "$OUT/obj/$tag.err"; fail=$((fail + 1))
    else
        [ -s "$OUT/obj/$tag.err" ] && { echo "--- asm warnings: ${asm#$ROOT/}"; cat "$OUT/obj/$tag.err"; }
        objs+=("$OUT/obj/$tag.o")
    fi
done

echo "compiled ${#objs[@]} objects, $fail failures"
[ "$fail" -eq 0 ] || exit 1

echo "=== linking ==="
# 747 object files blow past the Windows command line limit, so hand them over
# through a response file - exactly what CubeIDE's generated makefile does with
# @"objects.list".
: > "$OUT/objects.list"
for o in "${objs[@]}"; do
    # The linker wants forward slashes / no quoting issues on Windows paths.
    printf '%s\n' "$o" >> "$OUT/objects.list"
done
arm-none-eabi-gcc -o "$OUT/LED_2_Agent.elf" "@$OUT/objects.list" \
    -mcpu=cortex-m55 -T"$ROOT/Agent/STM32N647X0HXQ_ROMxspi2_RAMxspi1_agent.ld" \
    --specs=nosys.specs -Wl,-Map="$OUT/LED_2_Agent.map" -Wl,--gc-sections -static \
    -Wl,--cmse-implib -Wl,--out-implib="$OUT/secure_nsclib.o" \
    --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb \
    -Wl,--start-group -lc -lm -Wl,--end-group || { echo "LINK FAILED"; exit 1; }

arm-none-eabi-size "$OUT/LED_2_Agent.elf"
arm-none-eabi-objcopy -O binary "$OUT/LED_2_Agent.elf" "$OUT/LED_2_Agent.bin"
ls -la "$OUT/LED_2_Agent.bin"

echo "=== vector table check (what the FSBL does) ==="
python - "$OUT/LED_2_Agent.bin" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
sp, rv = struct.unpack_from('<II', d, 0)
print("SP = 0x%08X  (must be inside 0x34000000..0x34200000)" % sp)
print("RV = 0x%08X  (must be inside 0x70000000..0x7C000000)" % rv)
ok_sp = 0x34000000 <= sp <= 0x34200000
ok_rv = 0x70000000 <= rv < 0x7C000000
print("image size = %d bytes" % len(d))
print("bm_image_sane() would return: %s" % ("PASS" if (ok_sp and ok_rv and len(d) > 8) else "FAIL"))
sys.exit(0 if (ok_sp and ok_rv) else 1)
PY

echo "=== boot-record / OTA symbols in the agent image ==="
# Expected to be PRESENT: everything the receiving path needs.
# Expected to be ABSENT (dropped by --gc-sections because nothing calls them in
# this image): ota_confirm_poll, ota_request_agent, ota_dbg_ladder - the agent
# has no UI and must never confirm a trial boot, so their absence is correct.
arm-none-eabi-nm -C "$OUT/LED_2_Agent.elf" | grep -E ' (ota_switch_slot|ota_bootrec_write|ota_bootrec_read|ota_store_to_next_slot|ota_slot_refresh|app_netxduo_init|tx_application_define|sup_thread_entry|ota_probe_run|Reset_Handler)$' | sort

echo "=== guard check: the boot-area writer must NOT be in the agent build ==="
if arm-none-eabi-nm -C "$OUT/LED_2_Agent.elf" | grep -q ota_flash_write_image; then
    echo "FAIL: ota_flash_write_image leaked into the agent image"
    exit 1
else
    echo "OK: ota_flash_write_image is compiled out (-DLED2_OTA_AGENT)"
fi
if arm-none-eabi-nm -C "$OUT/LED_2_Agent.elf" | grep -qE ' (ota_confirm_poll|ota_request_agent)$'; then
    echo "FAIL: confirmation path leaked into the agent image"
    exit 1
else
    echo "OK: no confirmation path either"
fi

echo "=== section addresses (must live in the 0x714xxxxx partition) ==="
arm-none-eabi-objdump -h "$OUT/LED_2_Agent.elf" | grep -E 'isr_vector|\.text|\.data' | head -5
