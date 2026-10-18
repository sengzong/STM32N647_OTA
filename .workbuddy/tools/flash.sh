#!/bin/bash
# ---------------------------------------------------------------------------
# LED_2_noai flashing helper - wraps the STM32CubeProgrammer CLI.
#
# All three images live in the external NOR (XSPI2 mapped at 0x70000000), so
# every erase/write needs an external memory loader, and that loader is board
# specific:
#   F:/STM32CubeProgrammer/bin/ExternalLoader/
#       MX25UM25645G_ATK-CNN647B_ExtMemLoader.stldr
# (The older note in LOCAL_NOTES pointing at F:\atk_loader\... was stale.)
#
# Usage:
#   bash .workbuddy/tools/flash.sh probe      list connected ST-LINK probes
#   bash .workbuddy/tools/flash.sh erase      erase the whole external NOR only
#   bash .workbuddy/tools/flash.sh all        erase all, then write all 3 images
#   bash .workbuddy/tools/flash.sh fsbl       write fsbl_universal.hex  (0x70000000)
#   bash .workbuddy/tools/flash.sh appli      write appli.hex           (0x70100400)
#   bash .workbuddy/tools/flash.sh agent      write agent.hex           (0x71400400)
#   bash .workbuddy/tools/flash.sh stack      write appli.hex + agent.hex
#
# No address argument is passed to -w on purpose: the ihex records already carry
# their own addresses, and CubeProgrammer uses the file's addresses for hex/srec
# input - the command line address only matters for raw .bin. Passing one is at
# best redundant, at worst confusing.
#
# Boards: writes REFUSE to start when the detected probe looks like the
# NUCLEO-C542RC (that is the C542 project board). The loader above belongs to the
# ATK-CNN647B / STM32N647 board only. FORCE=1 overrides.
#
# Boot mode switches (ATK-CNN647B): BOOT1=1 + BOOT0=0 for download,
# BOOT0=0 for run. Power-cycle after flashing.
#
# ASCII-only on purpose (GBK console host).
# ---------------------------------------------------------------------------

set -u

# ----------------------------- path helpers -------------------------------
# arm-none-eabi-* and CubeProgrammer are native Windows .exe: they cannot
# resolve the MSYS "/f/..." form that pwd prints. Convert before handing over.
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

ScriptDir="$(cd "$(dirname "$0")" && pwd)"
RootWin="$(to_win "$(cd "$ScriptDir/../.." && pwd)")"
BinDir="$RootWin/Binary"

PRG="${STM32_PRG:-F:/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe}"
LOADER="${STM32_LOADER:-F:/STM32CubeProgrammer/bin/ExternalLoader/MX25UM25645G_ATK-CNN647B_ExtMemLoader.stldr}"

[ -x "$PRG" ] || { echo "flash: CubeProgrammer CLI not found at $PRG"; echo "       override with STM32_PRG=<path>"; exit 1; }
[ -f "$LOADER" ] || { echo "flash: external loader not found at $LOADER"; echo "       override with STM32_LOADER=<path>"; exit 1; }

CONN=(-c port=SWD ap=1 mode=HOTPLUG -halt -el "$LOADER")

# ------------------------------ board guard --------------------------------
detected_board() {
    "$PRG" -l 2>&1 | sed -n 's/^[[:space:]]*Board Name[[:space:]]*:[[:space:]]*//p' | head -1 | tr -d '\r'
}

board_guard() {
    local name
    name="$(detected_board)"
    if [ -z "$name" ]; then
        # Some probes report no board name at all; fall back to a probe check.
        if ! "$PRG" -l 2>&1 | grep -q 'ST-Link Probe'; then
            echo "flash: no ST-LINK probe detected. Check USB cable and board power."
            return 1
        fi
        echo "flash: probe detected but it reports no board name - continuing."
        return 0
    fi
    echo "flash: detected board = $name"
    case "$name" in
        *C542*)
            echo
            echo "!! REFUSED - this looks like the NUCLEO-C542RC, i.e. the C542 project"
            echo "!! board, not the STM32N647 / ATK-CNN647B this firmware targets."
            echo "!! Loader pinned for this project:"
            echo "!!   $LOADER"
            echo "!! Plug in the N647 board, or re-run with FORCE=1 if you are certain."
            [ "${FORCE:-0}" = "1" ] || return 1
            echo "!! FORCE=1 given - continuing anyway."
            ;;
    esac
    return 0
}

# ------------------------------- operations --------------------------------
img() {
    local f="$BinDir/$1"
    [ -f "$f" ] || { echo "flash: missing $f - build the project first."; exit 1; }
    printf '%s' "$f"
}

write_one() {
    local label="$1" hex="$2"
    echo "--- writing $label  ($hex)"
    "$PRG" "${CONN[@]}" -w "$hex" -v || return 1
}

case "${1:-}" in
    probe)
        "$PRG" -l
        ;;

    erase)
        board_guard || exit 1
        echo "--- erasing the whole external NOR"
        "$PRG" "${CONN[@]}" -e all
        ;;

    fsbl)
        board_guard || exit 1
        write_one "fsbl_universal.hex" "$(img fsbl_universal.hex)"
        ;;

    appli)
        board_guard || exit 1
        write_one "appli.hex" "$(img appli.hex)"
        ;;

    agent)
        board_guard || exit 1
        write_one "agent.hex" "$(img agent.hex)"
        ;;

    stack)
        board_guard || exit 1
        write_one "appli.hex" "$(img appli.hex)" || exit 1
        write_one "agent.hex" "$(img agent.hex)" || exit 1
        ;;

    all)
        board_guard || exit 1
        echo "--- erasing the whole external NOR"
        "$PRG" "${CONN[@]}" -e all || exit 1
        write_one "fsbl_universal.hex" "$(img fsbl_universal.hex)" || exit 1
        write_one "appli.hex"          "$(img appli.hex)"          || exit 1
        write_one "agent.hex"          "$(img agent.hex)"          || exit 1
        echo
        echo "flash: done. Set BOOT0=0 (run mode) and power-cycle the board."
        ;;

    *)
        sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
        echo
        echo "Usage: bash .workbuddy/tools/flash.sh {probe|erase|all|fsbl|appli|agent|stack}"
        exit 1
        ;;
esac

exit $?
