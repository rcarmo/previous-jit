/*
 * uae2026_vm_alloc_unit.cpp
 *
 * Compilation unit that brings macemu's CrossPlatform/vm_alloc.cpp
 * into the Previous build under ENABLE_EXPERIMENTAL_UAE2026_JIT.
 *
 * Provides: vm_acquire, vm_release, vm_protect, and friends used by
 * the vendored UAE 2026 JIT compiler's cache allocation paths.
 */

#if defined(ENABLE_EXPERIMENTAL_UAE2026_JIT)

/* vm_alloc.cpp requires config.h for HAVE_MMAP_VM etc. */
#include "config.h"
#include <unistd.h>   /* getpagesize */

/* Pull in the source directly — it has its own header guard on vm_alloc.h */
#include "vm_alloc.cpp"

#endif /* ENABLE_EXPERIMENTAL_UAE2026_JIT */
