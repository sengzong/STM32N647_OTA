#!/bin/bash
# LED_2 FSBL postbuild: sign the FSBL image and emit the NOR-boot image in ihex.
#
#   1. STM32_SigningTool_CLI : prepend the ST FSBL header (magic "STM2") and the
#                              authentication tag  ->  <name>-trusted.bin
#   2. arm-none-eabi-objcopy  : wrap the signed blob as ihex based at 0x70000000
#
# Output: ../Binary/fsbl_universal.hex  (name kept identical to README / LOCAL_NOTES)
#
# Called from .cproject:
#   postbuildStep = bash "../postbuild.sh" "${cubeide_cubeprogrammer_path}"
# Manual run (plain make workflow, no CubeIDE GUI):
#   bash ../postbuild.sh /f/STM32CubeProgrammer/bin
#
# NOTE: ASCII-only comments on purpose (GBK console host).

set -eu

to_win() {
    cygpath -m "$1"
}
ProjectDir="$(to_win "$(dirname "$(readlink -f "$0")")")"
OutDir="$ProjectDir/../Binary"
Out="$OutDir/fsbl_universal.hex"

# ------------------------------ 1. signer ---------------------------------
# Arg 1 is the CubeProgrammer bin directory as handed over by CubeIDE. Fall back
# to a few well-known locations so a bare "make" still produces a flashable hex.
Signer=""
if [ "$#" -ge 1 ] && [ -n "$1" ]; then
    if   [ -x "$1/STM32_SigningTool_CLI.exe" ]; then Signer="$1/STM32_SigningTool_CLI.exe"
    elif [ -x "$1/STM32_SigningTool_CLI"     ]; then Signer="$1/STM32_SigningTool_CLI"
    fi
fi
if [ -z "$Signer" ]; then
    for c in \
        "${STM32_PRG_PATH:-}" \
        "${STM32_PRG_PATH:-}/bin" \
        "/f/STM32CubeProgrammer/bin" \
        "/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin" \
        "/c/ST/STM32CubeProgrammer/bin"
    do
        [ -n "$c" ] || continue
        if   [ -x "$c/STM32_SigningTool_CLI.exe" ]; then Signer="$c/STM32_SigningTool_CLI.exe"; break
        elif [ -x "$c/STM32_SigningTool_CLI"     ]; then Signer="$c/STM32_SigningTool_CLI";     break
        fi
    done
fi
if [ -z "$Signer" ]; then
    echo "postbuild: STM32_SigningTool_CLI not found."
    echo "           pass its directory as arg1, or set STM32_PRG_PATH."
    exit 1
fi
echo "postbuild: signer = $Signer"

# --------------------------- 2. build artifact -----------------------------
# Use only the artifact from this build directory. Never pick an old Debug bin.
Bin="${2:-$(to_win "$PWD")/LED_2_FSBL.bin}"

if [ -z "$Bin" ] || [ ! -f "$Bin" ]; then
    echo "postbuild: no *_FSBL.bin found under $ProjectDir - build the project first."
    exit 1
fi
echo "postbuild: input  = $Bin"

# ------------------------------- 3. sign ----------------------------------
# STM32N6 needs -align: SigningTool 2.21+ no longer pads payloads to +0x400.
Trusted="${Bin%.*}-trusted.${Bin##*.}"
[ ! -f "$Trusted" ] || chmod u+w "$Trusted"
rm -f "$Trusted"

"$Signer" -s -bin "$Bin" -nk -of 0x80000000 -t fsbl -o "$Trusted" -hv 2.3 -align -dump "$Trusted"
if [ ! -f "$Trusted" ]; then
    echo "postbuild: signing FAILED - $Trusted was not produced."
    exit 1
fi

python "$ProjectDir/../tools/verify_fsbl_image.py" --raw "$Bin" --signed "$Trusted"

# ------------------------------ 4. to ihex --------------------------------
if ! command -v arm-none-eabi-objcopy >/dev/null 2>&1; then
    echo "postbuild: arm-none-eabi-objcopy not on PATH."
    echo "           prepend the CubeIDE GNU tools bin directory to PATH."
    exit 1
fi

mkdir -p "$OutDir"
arm-none-eabi-objcopy -I binary "$Trusted" --change-addresses 0x70000000 -O ihex "$Out.pending"
mv -f "$Out.pending" "$Out"
if [ ! -f "$Out" ]; then
    echo "postbuild: objcopy FAILED - $Out was not produced."
    exit 1
fi

echo "postbuild: output = $Out"
ls -la "$Out"
echo -n "postbuild: first record = "
head -n 1 "$Out"
echo "postbuild: sign + ihex done (base 0x70000000)"
