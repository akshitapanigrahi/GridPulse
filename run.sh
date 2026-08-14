#!/usr/bin/env bash
#
# GRID PULSE - one command, zero installation.
#
#   ./run.sh                 play; uses the keypad if it is plugged in, keyboard if not
#   ./run.sh --keyboard      keyboard mode, no serial port opened at all
#   ./run.sh --hardware      require the keypad; fail with an explanation if absent
#   ./run.sh --selftest      open the keypad calibration screen
#   ./run.sh --replay FILE   replay a recorded session
#   ./run.sh --test          run every test suite
#   ./run.sh --flash         copy the prebuilt firmware onto a Pico in BOOTSEL mode
#   ./run.sh --help          full option list
#
# POSIX-ish bash, no exotic dependencies.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

# --- pretty output, but only when a terminal is actually attached ---------------
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    BOLD=$'\033[1m'; DIM=$'\033[2m'; RED=$'\033[31m'; YELLOW=$'\033[33m'
    GREEN=$'\033[32m'; CYAN=$'\033[36m'; RESET=$'\033[0m'
else
    BOLD=""; DIM=""; RED=""; YELLOW=""; GREEN=""; CYAN=""; RESET=""
fi

say()  { printf '%s\n' "$*"; }
info() { printf '%s%s%s\n' "$CYAN" "$*" "$RESET"; }
warn() { printf '%s%s%s\n' "$YELLOW" "$*" "$RESET" >&2; }
fail() { printf '%s%s%s\n' "$RED" "$*" "$RESET" >&2; }
ok()   { printf '%s%s%s\n' "$GREEN" "$*" "$RESET"; }

usage() {
    cat <<'EOF'
GRID PULSE - a bit-rate maximisation game.

USAGE
    ./run.sh [options]

OPTIONS
    --keyboard          Play on the keyboard. No serial port is opened.
    --hardware          Require the 5x5 keypad; exit with a reason if it is absent.
    --port DEV          Use this serial device instead of auto-detecting.
    --selftest          Open the keypad calibration screen on launch.
    --replay FILE       Replay a recorded .jsonl session at its original timing.
    --replay-speed N    Multiplier for --replay. 10 skips the idle stretches.
    --terminal          Play and calibrate in the terminal: no browser, no page
                        served. Needs the keypad.
    --link              Measure the USB link and report it, then exit.
    --no-browser        Do not open a browser window.
    --no-log            Do not write session logs to logs/.
    --http-port N       Preferred localhost port (default 8765).
    --test              Run every test suite and exit nonzero on any failure.
    --flash             Copy the prebuilt firmware onto a Pico in BOOTSEL mode.
    --build-firmware    Build the firmware from source (needs an ARM toolchain).
    --verbose           Print extra diagnostics.
    -h, --help          This message.

WITH NO OPTIONS
    Looks for the keypad. Uses it if present, otherwise plays on the keyboard.
    Both are real, reportable scores.

EOF
}

# --- python discovery ------------------------------------------------------------
#
# 3.8 is the floor: the host code uses nothing newer, and 3.8 is what ships on older
# but still current distributions.
find_python() {
    local candidate
    for candidate in python3 python3.13 python3.12 python3.11 python3.10 python3.9 \
                     python3.8 python; do
        if command -v "$candidate" >/dev/null 2>&1; then
            if "$candidate" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' \
                 >/dev/null 2>&1; then
                printf '%s' "$candidate"
                return 0
            fi
        fi
    done
    return 1
}

require_python() {
    if ! PYTHON="$(find_python)"; then
        fail "Python 3.8 or newer is required to serve the game, and none was found."
        say ""
        say "  macOS : brew install python3     (or install the Xcode Command Line Tools)"
        say "  Debian: sudo apt install python3"
        say "  Fedora: sudo dnf install python3"
        say ""
        say "${BOLD}You can still play right now without it:${RESET}"
        say "  open the file  ${BOLD}web/play.html${RESET}  in any browser."
        return 1
    fi
    return 0
}

# --- serial port discovery --------------------------------------------------------
list_ports() {
    case "$(uname -s)" in
        Darwin) ls /dev/cu.usbmodem* 2>/dev/null ;;
        Linux)  ls /dev/ttyACM* 2>/dev/null ;;
        *)      ls /dev/ttyACM* /dev/cu.usbmodem* 2>/dev/null ;;
    esac
}

# On Linux a device that exists but cannot be opened is the single most common
# stumbling block. Detect it specifically and print the exact fix.
check_linux_permissions() {
    local port="$1"
    [ "$(uname -s)" = "Linux" ] || return 0
    [ -e "$port" ] || return 0
    if [ -r "$port" ] && [ -w "$port" ]; then
        return 0
    fi
    warn "The keypad is at $port but this user cannot open it."
    say ""
    say "  Add yourself to the 'dialout' group, then log out and back in:"
    say "      ${BOLD}sudo usermod -aG dialout \$USER${RESET}"
    say ""
    say "  Or install the udev rule shipped with this project, which grants access"
    say "  to this device specifically without changing your groups:"
    say "      ${BOLD}sudo cp docs/99-grid-pulse.rules /etc/udev/rules.d/${RESET}"
    say "      ${BOLD}sudo udevadm control --reload-rules && sudo udevadm trigger${RESET}"
    say ""
    say "  ${DIM}Falling through to keyboard mode for now.${RESET}"
    return 1
}

choose_port() {
    local ports
    mapfile -t ports < <(list_ports) 2>/dev/null || {
        # mapfile is bash 4+; macOS ships bash 3.2, so fall back to a read loop.
        ports=()
        while IFS= read -r line; do ports+=("$line"); done < <(list_ports)
    }

    if [ "${#ports[@]}" -eq 0 ]; then
        return 1
    fi
    if [ "${#ports[@]}" -eq 1 ]; then
        printf '%s' "${ports[0]}"
        return 0
    fi

    # More than one candidate. Ask, but only if someone is there to answer.
    if [ ! -t 0 ]; then
        printf '%s' "${ports[0]}"
        return 0
    fi
    say "Several serial devices look like they could be the keypad:" >&2
    local index=1
    for port in "${ports[@]}"; do
        say "  $index) $port" >&2
        index=$((index + 1))
    done
    printf 'Which one? [1-%d, or Enter for keyboard mode] ' "${#ports[@]}" >&2
    local answer
    read -r answer
    if [ -z "$answer" ]; then
        return 1
    fi
    if [ "$answer" -ge 1 ] 2>/dev/null && [ "$answer" -le "${#ports[@]}" ] 2>/dev/null; then
        printf '%s' "${ports[$((answer - 1))]}"
        return 0
    fi
    return 1
}

# --- test runner --------------------------------------------------------------------
run_tests() {
    local failures=0
    local jsc="/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc"

    say "${BOLD}== golden vectors are current ==${RESET}"
    if "$PYTHON" tools/gen_vectors.py --check; then ok "  ok"; else
        fail "  FAILED"; failures=$((failures + 1)); fi

    say ""
    say "${BOLD}== file:// reachability ==${RESET}"
    if "$PYTHON" tools/check_offline.py; then ok "  ok"; else
        fail "  FAILED"; failures=$((failures + 1)); fi

    say ""
    say "${BOLD}== native C++ logic ==${RESET}"
    if command -v make >/dev/null 2>&1 && command -v c++ >/dev/null 2>&1; then
        if make -s -C tests/native; then ok "  ok"; else
            fail "  FAILED"; failures=$((failures + 1)); fi
    else
        fail "  SKIPPED: a C++ compiler and make are required and were not found"
        failures=$((failures + 1))
    fi

    say ""
    say "${BOLD}== host (Python) ==${RESET}"
    if "$PYTHON" -m unittest discover -s tests/host -p 'test_*.py' -q; then
        ok "  ok"; else fail "  FAILED"; failures=$((failures + 1)); fi

    say ""
    say "${BOLD}== JavaScript game core ==${RESET}"
    # node first, then macOS's built-in JavaScriptCore, then give up loudly. A silently
    # skipped suite must never read as a pass.
    if command -v node >/dev/null 2>&1; then
        if node web/tests/core_test.js && node web/tests/ui_test.js; then
            ok "  ok (node)"
        else
            fail "  FAILED"; failures=$((failures + 1))
        fi
    elif [ -x "$jsc" ]; then
        local output
        output="$("$jsc" web/core/rng.js web/core/scoring.js web/core/alphabet.js \
                        web/core/boardmap.js web/core/session.js web/tests/sha256.js \
                        web/vectors.gen.js web/tests/core_test.js 2>&1)"
        say "$output"
        # jsc's quit() does not set an exit code, so the sentinel line is the verdict.
        if printf '%s' "$output" | grep -q 'GRIDPULSE_JS_TESTS: PASS'; then
            ok "  ok (JavaScriptCore)"
            warn "  note: the UI suite needs node; only the core suite ran"
        else
            fail "  FAILED"; failures=$((failures + 1))
        fi
    else
        fail "  FAILED: no JavaScript engine found (install node, or open web/tests.html)"
        failures=$((failures + 1))
    fi

    say ""
    if [ "$failures" -eq 0 ]; then
        ok "${BOLD}ALL SUITES PASSED${RESET}"
        return 0
    fi
    fail "${BOLD}$failures SUITE(S) FAILED${RESET}"
    return 1
}

# --- firmware -------------------------------------------------------------------------
flash_firmware() {
    local uf2="firmware/prebuilt/grid_pulse.uf2"
    if [ ! -f "$uf2" ]; then
        fail "No prebuilt firmware at $uf2"
        say "Build it with: ./run.sh --build-firmware"
        return 1
    fi

    local mount=""
    for candidate in /Volumes/RPI-RP2 /Volumes/RP2350 \
                     "/media/$USER/RPI-RP2" "/media/$USER/RP2350" \
                     /run/media/"$USER"/RPI-RP2; do
        if [ -d "$candidate" ]; then mount="$candidate"; break; fi
    done

    if [ -z "$mount" ]; then
        fail "No Pico in BOOTSEL mode was found."
        say ""
        say "  1. Unplug the board."
        say "  2. Hold the BOOTSEL button down."
        say "  3. Plug it back in, then release BOOTSEL."
        say "  4. A drive called RPI-RP2 appears. Run this again."
        return 1
    fi

    info "Copying $uf2 to $mount ..."

    # -X asks BSD/macOS cp not to replicate extended attributes. macOS stamps built
    # and downloaded files with com.apple.provenance, and the RP2040's BOOTSEL volume
    # is FAT with no xattr support - so cp writes the firmware, the board reboots and
    # unmounts the volume mid-copy, and cp then fails on the xattr and exits non-zero.
    # A successful flash reported as "Copy failed." is worse than useless. GNU cp has
    # no -X and does not copy xattrs by default, hence the fallback.
    if cp -X "$uf2" "$mount/" 2>/dev/null || cp "$uf2" "$mount/" 2>/dev/null; then
        ok "Flashed. The board will reboot and reconnect as a serial device."
        say "Now run: ./run.sh"
        return 0
    fi

    # cp's exit status is not the success condition: the volume disappearing is. If
    # the board rebooted, the firmware landed, whatever cp thought of the attributes.
    sleep 1
    if [ ! -d "$mount" ]; then
        ok "Flashed. The board rebooted and is reconnecting as a serial device."
        say "Now run: ./run.sh"
        return 0
    fi

    fail "Copy failed and the board is still in BOOTSEL mode."
    say "The volume $mount is still mounted, so the firmware did not land."
    return 1
}

build_firmware() {
    if ! command -v cmake >/dev/null 2>&1; then
        fail "cmake is required to build the firmware and was not found."
        say "The prebuilt firmware/prebuilt/grid_pulse.uf2 is committed; use --flash."
        return 1
    fi
    if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        fail "arm-none-eabi-gcc is required to build the firmware and was not found."
        say ""
        say "  Install ARM's toolchain (it bundles newlib, which Homebrew's does not):"
        say "  https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads"
        say ""
        say "The prebuilt firmware/prebuilt/grid_pulse.uf2 is committed; use --flash."
        return 1
    fi
    if [ -z "${PICO_SDK_PATH:-}" ]; then
        fail "PICO_SDK_PATH is not set."
        say "  git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk"
        say "  export PICO_SDK_PATH=\$PWD/pico-sdk"
        return 1
    fi

    info "Building firmware ..."
    ( cd firmware && cmake -B build -S . && cmake --build build -j ) || return 1
    cp firmware/build/grid_pulse.uf2 firmware/prebuilt/grid_pulse.uf2
    ok "Built firmware/prebuilt/grid_pulse.uf2"
}

# --- argument parsing -------------------------------------------------------------------
MODE="auto"
EXPLICIT_PORT=""
REPLAY_FILE=""
REPLAY_SPEED=""
PASSTHROUGH=()
DO_TEST=0
DO_FLASH=0
DO_BUILD=0

while [ $# -gt 0 ]; do
    case "$1" in
        --keyboard)  MODE="keyboard"; shift ;;
        --hardware)  MODE="hardware"; shift ;;
        --port)      EXPLICIT_PORT="${2:-}"; shift 2 ;;
        --replay)    REPLAY_FILE="${2:-}"; shift 2 ;;
        --replay-speed) REPLAY_SPEED="${2:-}"; shift 2 ;;
        --selftest)  PASSTHROUGH+=("--selftest"); MODE="hardware"; shift ;;
        --no-browser|--no-log|--verbose) PASSTHROUGH+=("$1"); shift ;;
        --terminal)  PASSTHROUGH+=("--terminal"); MODE="hardware"; shift ;;
        --link)      PASSTHROUGH+=("--link"); MODE="hardware"; shift ;;
        --http-port) PASSTHROUGH+=("--http-port" "${2:-}"); shift 2 ;;
        --replay-speed) PASSTHROUGH+=("--replay-speed" "${2:-}"); shift 2 ;;
        --test)      DO_TEST=1; shift ;;
        --flash)     DO_FLASH=1; shift ;;
        --build-firmware) DO_BUILD=1; shift ;;
        -h|--help)   usage; exit 0 ;;
        *)           fail "Unknown option: $1"; say ""; usage; exit 2 ;;
    esac
done

# --- dispatch --------------------------------------------------------------------------
if [ "$DO_FLASH" -eq 1 ]; then
    flash_firmware; exit $?
fi

if [ "$DO_BUILD" -eq 1 ]; then
    build_firmware; exit $?
fi

if [ "$DO_TEST" -eq 1 ]; then
    require_python || exit 1
    run_tests; exit $?
fi

require_python || exit 1

ARGS=()

if [ -n "$REPLAY_FILE" ]; then
    if [ ! -f "$REPLAY_FILE" ]; then
        fail "No such log file: $REPLAY_FILE"
        exit 1
    fi
    ARGS+=("--replay" "$REPLAY_FILE")
    [ -n "${REPLAY_SPEED:-}" ] && ARGS+=("--replay-speed" "$REPLAY_SPEED")

elif [ "$MODE" = "keyboard" ]; then
    ARGS+=("--keyboard")

else
    PORT="$EXPLICIT_PORT"
    if [ -z "$PORT" ]; then
        PORT="$(choose_port)" || PORT=""
    fi

    if [ -n "$PORT" ]; then
        if check_linux_permissions "$PORT"; then
            info "Keypad: $PORT"
            ARGS+=("--port" "$PORT")
            [ "$MODE" = "hardware" ] && ARGS+=("--hardware")
        else
            # The permission helper has already explained the fix in detail.
            if [ "$MODE" = "hardware" ]; then
                fail "--hardware was requested but the device cannot be opened."
                exit 1
            fi
            ARGS+=("--keyboard")
        fi
    else
        if [ "$MODE" = "hardware" ]; then
            fail "--hardware was requested but no keypad was found."
            say "Plug it in, or run ./run.sh --keyboard to play on the keyboard."
            exit 1
        fi
        # THE IMPORTANT PATH: no hardware is completely normal. One friendly line,
        # then straight into a playable game.
        #
        # Deliberately NOT --keyboard. That flag means "never open a serial port",
        # which also switches off the hot-plug watcher - so plugging the keypad in
        # after launch would do nothing, which is the one moment someone is most
        # likely to do it. Starting without a port instead leaves the host watching,
        # and the UI offers the keypad the moment it appears.
        say "${DIM}No keypad detected - playing on the keyboard. Plug one in any time.${RESET}"
    fi
fi

ARGS+=("${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}")

# Clean shutdown: the server restores the tty and closes the port in its own finally
# block, so forwarding the signal is all that is needed here.
SERVER_PID=""
shutdown() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    exit 0
}
trap shutdown INT TERM

# Terminal mode reads from stdin, so it has to run in the FOREGROUND.
#
# A backgrounded job does not own the terminal: its read either gets EOF or is stopped
# with SIGTTIN, so the menu appeared and the process exited immediately with no way to
# type anything at it. Everything else is a server that never reads stdin, and is
# backgrounded so this script can forward signals to it.
for arg in ${ARGS[@]+"${ARGS[@]}"}; do
    if [ "$arg" = "--terminal" ]; then
        exec "$PYTHON" "$REPO_ROOT/host" "${ARGS[@]+"${ARGS[@]}"}"
    fi
done

"$PYTHON" "$REPO_ROOT/host" "${ARGS[@]+"${ARGS[@]}"}" &
SERVER_PID=$!
wait "$SERVER_PID"
exit $?
