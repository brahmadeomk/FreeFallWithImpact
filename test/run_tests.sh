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
/tmp/ctx311_tests

# Third pass: same suite with the stack instrumentation switched on.
#
# That code is invisible to the two builds above -- it lives behind
# #ifdef CTX311_STACK_DEBUG and only ever compiles for a soak image, so
# it is exactly the kind of thing that rots without anyone noticing.
# Building it here catches that.
#
# It must also be INERT: turning the flag on may not disturb any
# register semantics. On the host stackUnusedBytes() returns 0, so
# register 48 stays 0 and every assertion above must still hold. If
# this pass ever diverges from the pass above, the debug build has
# started changing behaviour it has no business changing.
echo
echo "=============================================="
echo " CTX311 rev A  (-DCTX311_STACK_DEBUG)"
echo "=============================================="
g++ $CXX_FLAGS -DCTX311_STACK_DEBUG -x c++ test_ctx311.cpp -o /tmp/ctx311_dbg_tests
exec /tmp/ctx311_dbg_tests
