#!/bin/sh
# Build either sketch for an Arduino Nano (ATmega328P, 16 MHz) and print
# flash and static SRAM. This is what produces the figures in
# docs/RESOURCE_BUDGET.md.
#
# Why this exists rather than "open it in the Arduino IDE": the numbers in
# the architecture document were estimated from reading the source, and an
# estimate of static SRAM is not something you can sanity-check by eye --
# .bss is dominated by whatever the libraries drag in, not by the sketch's
# own declarations. It needed a real link.
#
# Usage:
#   tools/build_avr.sh <sketch-dir> <core-dir> <adxl-driver-dir> [outdir]
#
#   sketch-dir       CTX310_Vibration_revH/ or CTX311_LossOfSupport_revA/
#   core-dir         a checkout of github.com/arduino/ArduinoCore-avr
#   adxl-driver-dir  directory holding mFFT_SparkFun_ADXL345.cpp and its
#                    header. NOT in this repository -- see the note below.
#
# THE DRIVER IS THE CATCH. Both sketches do
#     #include "SparkFun_ADXL345-master/mFFT_SparkFun_ADXL345.cpp"
# and that file is not vendored here (README says so explicitly). Whatever
# you point this at contributes its own flash and .bss to the totals, so a
# build against a *substitute* driver does not give you the product's
# numbers. Point it at the real one before quoting any figure as final.
#
# Extra compiler flags come from $EXTRA. The one that matters:
#
#   EXTRA=-DCTX311_STACK_DEBUG tools/build_avr.sh CTX311_LossOfSupport_revA ...
#
# builds the stack high-water image for a soak run. That image reports
# minimum free SRAM in register 48 (a reserved hole in a release build)
# and MUST NOT be shipped. See docs/RESOURCE_BUDGET.md.
#
# Requires: gcc-avr, avr-libc, binutils-avr.
set -e

SKETCH="$1"; CORE="$2"; ADXL="$3"; OUT="${4:-build-avr}"
if [ -z "$SKETCH" ] || [ -z "$CORE" ] || [ -z "$ADXL" ]; then
  sed -n '2,30p' "$0"; exit 2
fi

NAME=$(basename "$SKETCH")
rm -rf "$OUT"; mkdir -p "$OUT/SparkFun_ADXL345-master" "$OUT/core"

cp "$SKETCH/$NAME.ino" "$OUT/sketch.cpp"
cp "$SKETCH/SimpleModbusSlave.cpp" "$SKETCH/SimpleModbusSlave.h" "$OUT/"
cp "$ADXL"/* "$OUT/SparkFun_ADXL345-master/"

cd "$OUT"

MCU="-mmcu=atmega328p -DF_CPU=16000000L -DARDUINO=10819 -DARDUINO_AVR_NANO -DARDUINO_ARCH_AVR $EXTRA"
OPT="-Os -ffunction-sections -fdata-sections"
INC="-I$CORE/cores/arduino -I$CORE/variants/eightanaloginputs \
     -I$CORE/libraries/SPI/src -I$CORE/libraries/EEPROM/src \
     -I$CORE/libraries/Wire/src -I."

avr-g++ -c -std=gnu++11 $OPT $MCU $INC sketch.cpp          -o sketch.o
avr-g++ -c -std=gnu++11 $OPT $MCU $INC SimpleModbusSlave.cpp -o modbus.o

# The core is rebuilt every time. It is a few seconds and it keeps the
# figures honest -- a stale core archive built with different flags would
# quietly change .bss.
for f in "$CORE"/cores/arduino/*.c "$CORE"/libraries/Wire/src/utility/*.c; do
  avr-gcc -c -std=gnu11 $OPT $MCU $INC "$f" -o "core/$(basename "$f").o" 2>/dev/null || true
done
for f in "$CORE"/cores/arduino/*.cpp "$CORE"/libraries/SPI/src/*.cpp \
         "$CORE"/libraries/Wire/src/*.cpp; do
  avr-g++ -c -std=gnu++11 $OPT $MCU $INC "$f" -o "core/$(basename "$f").o" 2>/dev/null || true
done

avr-gcc -Os -mmcu=atmega328p -Wl,--gc-sections -o out.elf \
        sketch.o modbus.o core/*.o -lm

echo
echo "=== $NAME ==="
avr-size --format=avr --mcu=atmega328p out.elf
DATA=$(avr-size --format=avr --mcu=atmega328p out.elf | awk '/^Data/{print $2}')
echo "static SRAM $DATA B of 2048 -- free $((2048 - DATA)) B ($(((2048 - DATA) * 100 / 2048))%)"
echo
echo "Reminder: free SRAM here is STATIC only. It is the space the stack and"
echo "any runtime growth must live in, not headroom that is known to be spare."
echo "The stack high-water figure that would settle that is not measured yet."
