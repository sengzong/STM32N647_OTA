#!/bin/bash
# Clean out-of-tree verification build of the FSBL project.
#
# Why this exists: the generated makefiles under FSBL/Debug are unusable for a
# command-line build - they hardcode the OLD project path
# (F:/stm32CubeIde/Project/LED_2) for the HAL and ExtMem sources and do not list
# bootmgr.c at all. This script compiles exactly the source set declared in
# FSBL/.project + FSBL/Core/Src, then links with the real linker script and the
# same flags CubeIDE uses.
#
# Usage:  bash fsbl_verify.sh [debug|release]

MODE="${1:-debug}"

ROOT="F:/stm32CubeIde/Project/LED_2_noai"
TC="/f/stm32CubeIde/STM32CubeIDE_1.17.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.12.3.rel1.win32_1.1.0.202410251130/tools/bin"

if [ "$MODE" = "release" ]; then
    OUT="C:/Users/ls/AppData/Local/Temp/fsbl_verify_rel"
    # .cproject Release: debug level g0, optimisation -Os, no -DDEBUG
    FLAGS="-Os"
else
    OUT="C:/Users/ls/AppData/Local/Temp/fsbl_verify"
    # .cproject Debug: debug level g3, no optimisation, -DDEBUG
    FLAGS="-g3 -DDEBUG -O0"
fi

export PATH="$TC:$PATH"
rm -rf "$OUT"
mkdir -p "$OUT/obj"
cd "$OUT" || exit 1

echo "mode=$MODE  flags=[$FLAGS]"
echo "toolchain: $(arm-none-eabi-gcc --version | head -1)"

INC="-I$ROOT/FSBL/Core/Inc \
-I$ROOT/Drivers/STM32N6xx_HAL_Driver/Inc \
-I$ROOT/Drivers/CMSIS/Device/ST/STM32N6xx/Include \
-I$ROOT/Drivers/STM32N6xx_HAL_Driver/Inc/Legacy \
-I$ROOT/Drivers/CMSIS/Include \
-I$ROOT/Middlewares/ST/STM32_ExtMem_Manager \
-I$ROOT/Middlewares/ST/STM32_ExtMem_Manager/boot \
-I$ROOT/Middlewares/ST/STM32_ExtMem_Manager/sal \
-I$ROOT/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp \
-I$ROOT/Middlewares/ST/STM32_ExtMem_Manager/psram \
-I$ROOT/Middlewares/ST/STM32_ExtMem_Manager/sdcard \
-I$ROOT/Middlewares/ST/STM32_ExtMem_Manager/user \
-I$ROOT/Drivers/BSP/HyperRAM"

CF="-mcpu=cortex-m55 -std=gnu11 $FLAGS -DUSE_HAL_DRIVER -DSTM32N647xx -ffunction-sections -fdata-sections -Wall -mcmse --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb"

SRCS="
$ROOT/FSBL/Core/Src/bootmgr.c
$ROOT/FSBL/Core/Src/extmem_manager.c
$ROOT/FSBL/Core/Src/main.c
$ROOT/FSBL/Core/Src/stm32n6xx_hal_msp.c
$ROOT/FSBL/Core/Src/stm32n6xx_it.c
$ROOT/FSBL/Core/Src/syscalls.c
$ROOT/FSBL/Core/Src/sysmem.c
$ROOT/FSBL/Core/Src/system_stm32n6xx_fsbl.c
$ROOT/Drivers/BSP/HyperRAM/hyperram.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cortex.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma_ex.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_exti.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_gpio.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr_ex.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc_ex.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart.c
$ROOT/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/boot/stm32_boot_xip.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/stm32_extmem.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/psram/stm32_psram_driver.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/sal/stm32_sal_sd.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/sal/stm32_sal_xspi.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/sdcard/stm32_sdcard_driver.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp/stm32_sfdp_data.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp/stm32_sfdp_driver.c
$ROOT/Middlewares/ST/STM32_ExtMem_Manager/user/stm32_user_driver.c
"

fail=0
objs=()
for src in $SRCS; do
    tag="$(echo "${src#$ROOT/}" | sed 's|/|_|g; s|\.c$||')"
    obj="$OUT/obj/$tag.o"
    if ! arm-none-eabi-gcc "$src" $CF $INC -c -o "$obj" 2> "$OUT/obj/$tag.err"; then
        echo "CC FAILED: $src"
        cat "$OUT/obj/$tag.err"
        fail=$((fail + 1))
    else
        if [ -s "$OUT/obj/$tag.err" ]; then
            echo "--- warnings: ${src#$ROOT/}"
            cat "$OUT/obj/$tag.err"
        fi
        objs+=("$obj")
    fi
done

if ! arm-none-eabi-gcc "$ROOT/FSBL/Core/Startup/startup_stm32n647x0hxq_fsbl.s" $CF $INC -c -o "$OUT/obj/startup.o" 2> "$OUT/obj/startup.err"; then
    echo "AS FAILED: startup"
    cat "$OUT/obj/startup.err"
    fail=$((fail + 1))
else
    objs+=("$OUT/obj/startup.o")
fi
echo "compiled ${#objs[@]} objects, $fail failures"
[ "$fail" -eq 0 ] || exit 1

echo "=== linking ==="
arm-none-eabi-gcc -o "$OUT/LED_2_FSBL.elf" "${objs[@]}" \
    -mcpu=cortex-m55 -T"$ROOT/FSBL/STM32N647X0HXQ_AXISRAM2_fsbl.ld" \
    --specs=nosys.specs -Wl,-Map="$OUT/LED_2_FSBL.map" -Wl,--gc-sections -static \
    -Wl,--cmse-implib -Wl,--out-implib="$OUT/secure_nsclib.o" \
    --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb \
    -Wl,--start-group -lc -lm -Wl,--end-group || { echo "LINK FAILED"; exit 1; }

arm-none-eabi-size "$OUT/LED_2_FSBL.elf"
arm-none-eabi-objcopy -O binary "$OUT/LED_2_FSBL.elf" "$OUT/LED_2_FSBL.bin"
ls -la "$OUT/LED_2_FSBL.bin"

echo "=== boot manager symbols at AXISRAM2 address ==="
grep -nE "^ *0x341[0-9a-f]+ +(BOOTMGR_Process|bm_do_switch|bm_do_rollback|bm_backup_create|bm_backup_restore|HAL_UART_MspInit)$" "$OUT/LED_2_FSBL.map" | head -20
