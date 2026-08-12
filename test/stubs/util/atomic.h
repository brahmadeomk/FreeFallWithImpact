/* Host-side stand-in for <util/atomic.h>. Test use only.
   The test is single threaded with no ISR, so an atomic section is just
   an ordinary block. */
#ifndef TEST_STUB_ATOMIC_H
#define TEST_STUB_ATOMIC_H

#define ATOMIC_RESTORESTATE
#define ATOMIC_FORCEON
#define ATOMIC_BLOCK(type)

#endif
