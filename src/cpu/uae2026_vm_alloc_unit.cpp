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

/* vm_alloc.cpp requires config.h for platform feature macros, but Previous's
 * generated config does not advertise mmap even on Linux.  Force the mmap path
 * here so executable JIT mappings are backed by mmap/mprotect instead of the
 * non-executable calloc fallback. */
#include "config.h"
#if defined(__linux__)
#  ifndef HAVE_MMAP_VM
#    define HAVE_MMAP_VM 1
#  endif
#  ifndef HAVE_MMAP_ANONYMOUS
#    define HAVE_MMAP_ANONYMOUS 1
#  endif
#  ifndef HAVE_SYS_MMAN_H
#    define HAVE_SYS_MMAN_H 1
#  endif
#  ifndef HAVE_UNISTD_H
#    define HAVE_UNISTD_H 1
#  endif
#endif
#include <unistd.h>   /* getpagesize */
#include <sys/mman.h>

/* Pull in the source directly — it has its own header guard on vm_alloc.h */
#include "vm_alloc.cpp"

#endif /* ENABLE_EXPERIMENTAL_UAE2026_JIT */
