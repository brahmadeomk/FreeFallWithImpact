#!/bin/sh
# Compile and run the host-side tests. No AVR toolchain needed:
# test/stubs stands in for the core, SPI, EEPROM and the ADXL driver.
#
# Both products are built and run. The VRM315-IM suite must keep passing
# unchanged -- if VRM315-IMLOS work breaks it, something that was working in
# the field has been broken.
set -e
cd "$(dirname "$0")"

CXX_FLAGS="-std=c++11 -Wall -Wextra -Wno-unused-parameter -I stubs"

echo "=============================================="
echo " VRM315-IM rev H  (impact + vibration)"
echo "=============================================="
g++ $CXX_FLAGS -x c++ test_vrm315_im.cpp -o /tmp/vrm315_im_tests
/tmp/vrm315_im_tests

echo
echo "=============================================="
echo " VRM315-IMLOS rev A  (loss of support)"
echo "=============================================="
g++ $CXX_FLAGS -x c++ test_vrm315_imlos.cpp -o /tmp/vrm315_imlos_tests
/tmp/vrm315_imlos_tests

# Third pass: same suite with the stack instrumentation switched on.
#
# That code is invisible to the two builds above -- it lives behind
# #ifdef VRM315_STACK_DEBUG and only ever compiles for a soak image, so
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
echo " VRM315-IMLOS rev A  (-DVRM315_STACK_DEBUG)"
echo "=============================================="
g++ $CXX_FLAGS -DVRM315_STACK_DEBUG -x c++ test_vrm315_imlos.cpp -o /tmp/vrm315_imlos_dbg_tests
exec /tmp/vrm315_imlos_dbg_tests
