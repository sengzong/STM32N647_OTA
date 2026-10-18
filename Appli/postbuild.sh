#!/bin/bash
# LED_2 Appli postbuild: 把 Release 的 *_Appli.bin 变成 0x70100400(XIP/NOR)的 ihex
#
# IMPORTANT: arm-none-eabi-objcopy is a native Windows .exe. It cannot resolve
# the MSYS/Cygwin "/f/..." form printed by readlink/pwd - it reports
# "No such file". Convert every path handed to it into the native "F:/..."
# form first (shell-side tests like [ -f ] accept either form).
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
Bin="$ProjectDir/Release/LED_2_Appli.bin"
[ -f "$Bin" ] || { echo "postbuild: cannot find $Bin"; exit 1; }
OutDir="$(dirname "$ProjectDir")/Binary"
mkdir -p "$OutDir"
arm-none-eabi-objcopy -I binary "$Bin" --change-addresses 0x70100400 -O ihex "$OutDir/appli.hex"
ls -la "$OutDir/appli.hex"
echo "postbuild: appli.hex done (base 0x70100400)"