#!/bin/sh
# Compile and run the host-side tests. No AVR toolchain needed:
# test/stubs stands in for the core, SPI, EEPROM and the ADXL driver.
#
# Both products are built and run. The CTX310 suite must keep passing
# unchanged -- if CTX311 work breaks it, something that was working in
# the field has been broken.
set -e
cd "$(dirname "$0")"

CXX_FLAGS="-std=c++11 -Wall -Wextra -Wno-unused-parameter -I stubs"

echo "=============================================="
echo " CTX310 rev H  (impact + vibration)"
echo "=============================================="
g++ $CXX_FLAGS -x c++ test_registers.cpp -o /tmp/ctx310_tests
/tmp/ctx310_tests

echo
echo "=============================================="
echo " CTX311 rev A  (loss of support)"
echo "=============================================="
g++ $CXX_FLAGS -x c++ test_ctx311.cpp -o /tmp/ctx311_tests
exec /tmp/ctx311_tests
