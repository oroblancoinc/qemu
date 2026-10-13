#!/bin/sh
# Build and run the bare-metal ESP32-C3 I2C controller test. See README.md.
#
# Usage: ./run.sh
# Env:   QEMU        path to qemu-system-riscv32
#                    (default: ../../build/qemu-system-riscv32)
#        CROSS_CC    riscv32 bare-metal C compiler
#                    (default: riscv32-esp-elf-gcc from PATH, else the newest
#                    toolchain under ~/.espressif)
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
QEMU="${QEMU:-$HERE/../../build/qemu-system-riscv32}"

if [ -z "${CROSS_CC:-}" ]; then
    if command -v riscv32-esp-elf-gcc >/dev/null 2>&1; then
        CROSS_CC=riscv32-esp-elf-gcc
    else
        CROSS_CC="$(ls -d "$HOME"/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc \
                    2>/dev/null | tail -1)"
    fi
fi

if [ -z "$CROSS_CC" ] || ! command -v "$CROSS_CC" >/dev/null 2>&1; then
    echo "SKIP: no riscv32 bare-metal compiler found; set CROSS_CC" >&2
    exit 77
fi
if [ ! -x "$QEMU" ]; then
    echo "FAIL: $QEMU is not executable; set QEMU" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# The LOAD segment is RWX because text and stack share one region; harmless
# here and not worth a second segment, so silence just that warning.
"$CROSS_CC" -march=rv32imc_zicsr -mabi=ilp32 -O1 -ffreestanding -nostdlib -Wall \
    -T "$HERE/i2ctest.ld" -o "$WORK/i2ctest.elf" \
    "$HERE/start.S" "$HERE/i2ctest.c" 2>&1 | grep -v "RWX permissions" || true
[ -f "$WORK/i2ctest.elf" ] || { echo "FAIL: compile failed" >&2; exit 1; }

# The test expects chips at 0x58 (reg 0x10 = 0x23) and 0x3a (reg 0x00 = 0x18),
# and nothing at 0x08 or 0x5b so the NACK path is covered. No pads are pinned,
# so pad gating stays out of the way.
timeout 60 "$QEMU" -nographic -machine 'esp32c3,oro-i2c=58:10=23;3a:00=18' \
    -kernel "$WORK/i2ctest.elf" > "$WORK/out.log" 2>&1 || true

sed -n '/I2CTEST START/,/I2CTEST \(ALL OK\|FAILURES\)/p' "$WORK/out.log"

if grep -q "I2CTEST ALL OK" "$WORK/out.log"; then
    exit 0
fi
echo "FAIL: see the checks above" >&2
exit 1
