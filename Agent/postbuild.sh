#!/bin/bash
# LED_2 Agent postbuild: wrap the built agent binary at its NOR XIP base.
#
#   input  : <newest>_Agent.bin produced by whichever configuration was built
#   output : ../Binary/agent.hex   ihex based at 0x71400400
#            (= NOR offset 0x1400000 + the 0x400 partition header)
#
# NO SIGNING. The FSBL reaches this image through bm_jump_to(), which only
# checks the vector table (SP inside internal RAM, Reset_Handler inside the XIP
# window) and jumps - it never authenticates. Signing is required only for the
# FSBL itself, because the STM32N6 boot ROM is what loads that one.
#
# Flash it with STM32CubeProgrammer together with fsbl_universal.hex:
#   agent.hex sits at 0x71400000.. so it never overlaps the boot area,
#   the three repository slots or the golden backup.
#
# NOTE: ASCII-only comments on purpose (GBK console host).

set -u

# IMPORTANT: arm-none-eabi-objcopy is a native Windows .exe. It cannot resolve
# the MSYS/Cygwin "/f/..." form that readlink and pwd print by default - it
# reports "No such file". Every path handed to it must therefore be converted
# into the native "F:/..." form first, while the shell-side tests ([ -f ]) work
# with either form.
to_win() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
        return
    fi
    case "$1" in
        /[a-zA-Z]/*)
            _d=$(printf '%s' "$1" | cut -c2 | tr 'a-z' 'A-Z')
            _r=$(printf '%s' "$1" | cut -c3-)
            printf '%s' "${_d}:${_r}" ;;
        *)
            printf '%s' "$1" ;;
    esac
}

if command -v readlink >/dev/null 2>&1; then
    _self="$(readlink -f "$0")"
else
    _self="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
fi
ProjectDir="$(to_win "$(dirname "$_self")")"
OutDir="$(dirname "$ProjectDir")/Binary"
Out="$OutDir/agent.hex"

# --------------------------- build artifact -------------------------------
Bin=""
Newest=""
while IFS= read -r line; do
    ts="${line%% *}"
    path="${line#* }"
    if [ -z "$Newest" ] || [ "$ts" -gt "$Newest" ]; then Newest="$ts"; Bin="$path"; fi
done <<EOF
$(find "$ProjectDir" -name '*_Agent.bin' -printf '%T@ %p\n' 2>/dev/null)
EOF

if [ -z "$Bin" ] || [ ! -f "$Bin" ]; then
    echo "postbuild: no *_Agent.bin found under $ProjectDir - build the project first."
    exit 1
fi
echo "postbuild: input  = $Bin"
echo "postbuild: size   = $(stat -c%s "$Bin") bytes"

# ------------------------------ to ihex -----------------------------------
if ! command -v arm-none-eabi-objcopy >/dev/null 2>&1; then
    echo "postbuild: arm-none-eabi-objcopy not on PATH."
    echo "           prepend the CubeIDE GNU tools bin directory to PATH."
    exit 1
fi

mkdir -p "$OutDir"
arm-none-eabi-objcopy -I binary "$Bin" --change-addresses 0x71400400 -O ihex "$Out"
if [ ! -f "$Out" ]; then
    echo "postbuild: objcopy FAILED - $Out was not produced."
    exit 1
fi

echo "postbuild: output = $Out"
ls -la "$Out"
echo -n "postbuild: first record = "
head -n 1 "$Out"
echo "postbuild: agent.hex done (base 0x71400400)"
