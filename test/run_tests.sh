#!/bin/sh
# Compile and run the host-side register tests. No AVR toolchain needed:
# test/stubs stands in for the core, SPI, EEPROM and the ADXL driver.
set -e
cd "$(dirname "$0")"
g++ -std=c++11 -Wall -Wextra -Wno-unused-parameter \
    -I stubs -x c++ test_registers.cpp -o /tmp/ctx310_tests
exec /tmp/ctx310_tests
