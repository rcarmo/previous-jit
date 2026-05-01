/*
 * uae2026_vendored_preamble.h  (private, not installed)
 *
 * Type-compatibility preamble shared by uae2026_compiler_unit.cpp and
 * uae2026_linker_stubs.cpp.  Provides the Basilisk-style types that the
 * vendored UAE 2026 headers expect, activates the AArch64 flag path in
 * m68k.h, and undef's Previous-specific macros that conflict.
 *
 * Include this file FIRST in any compilation unit that includes vendored
 * uae_cpu_2026 headers, before any other include.
 */

#ifndef PREVIOUS_UAE2026_VENDORED_PREAMBLE_H
#define PREVIOUS_UAE2026_VENDORED_PREAMBLE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* The vendored VM allocator only emits mmap/mprotect-backed executable
 * mappings when these feature macros are present. Previous's generated
 * config.h does not define them, which otherwise makes vm_alloc.cpp fall
 * back to calloc(): writable but non-executable heap memory, causing an
 * immediate SIGSEGV at the first generated AArch64 instruction. */
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

/* Previous-native base types */
#include "sysdeps.h"

/* sysconfig.h (via compat.h via sysdeps.h) defines FULLMMU and
 * NATMEM_OFFSET=natmem_offset.  Both must be cleared before the
 * vendored headers are processed.                                      */
#ifdef FULLMMU
#  undef FULLMMU
#endif
#ifdef NATMEM_OFFSET
#  undef NATMEM_OFFSET
#endif

/* Basilisk-style type aliases expected by the vendored headers */
#ifndef UAE2026_VENDORED_TYPE_ALIASES_DEFINED
#define UAE2026_VENDORED_TYPE_ALIASES_DEFINED 1
typedef uae_u8          uint8;
typedef uae_u16         uint16;
typedef uae_u32         uint32;
typedef uae_u64         uint64;
typedef int             int32;
/* uintptr: match compemu_arm.h's typedef uae_u64 uintptr exactly */
typedef uae_u64         uintptr;
#endif

/* ALWAYS_INLINE / UNUSED */
#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE inline __attribute__((always_inline))
#endif
#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

/* Big-endian M68k ↔ little-endian AArch64 host byte accessors */
static ALWAYS_INLINE uae_u32 do_byteswap_16(uae_u32 v) { return __builtin_bswap16((uint16_t)v); }
static ALWAYS_INLINE uae_u32 do_byteswap_32(uae_u32 v) { return __builtin_bswap32(v); }
static ALWAYS_INLINE uae_u32 do_get_mem_long(uae_u32 *a) { return __builtin_bswap32(*a); }
static ALWAYS_INLINE uae_u32 do_get_mem_word(uae_u16 *a) { return __builtin_bswap16(*a); }
#define do_get_mem_byte(a) ((uae_u32)*((uae_u8 *)(a)))
static ALWAYS_INLINE void do_put_mem_long(uae_u32 *a, uae_u32 v) { *a = __builtin_bswap32(v); }
static ALWAYS_INLINE void do_put_mem_word(uae_u16 *a, uae_u32 v) { *a = __builtin_bswap16((uint16_t)v); }
#define do_put_mem_byte(a, v) (*(uae_u8 *)(a) = (uae_u8)(v))

/* Activate AArch64 flag path: flag_struct {nzcv, x} + pure-C macros */
#define OPTIMIZED_FLAGS
#ifndef CPU_aarch64
#  define CPU_aarch64
#endif
#ifndef AARCH64_ASSEMBLY
#  define AARCH64_ASSEMBLY
#endif

/* m68k.h must be the vendored version (uae_cpu_2026/m68k.h, guard M68K_FLAGS_H).
 * Including it here via the explicit path sets the guard and prevents
 * src/cpu/m68k.h (no guard) from redefining the flag struct later.    */
#include "uae_cpu_2026/m68k.h"

/* Activate direct-addressing memory path (get_long/word/byte inlines) */
#define DIRECT_ADDRESSING 1

/* MEMBaseDiff: declared extern in memory.h under #if DIRECT_ADDRESSING */
extern uintptr MEMBaseDiff;

/* UAE runtime mode flags */
#ifndef JIT
#  define JIT 1
#endif
#ifndef USE_JIT
#  define USE_JIT 1
#endif
#ifndef UAE
#  define UAE 1
#endif
#ifndef CPU_AARCH64
#  define CPU_AARCH64 1
#endif
#ifndef CPU_64_BIT
#  define CPU_64_BIT 1
#endif
#ifndef USE_JIT2
#  define USE_JIT2 1
#endif

/* Block src/cpu/newcpu.h (guard UAE_NEWCPU_H) after the vendored one
 * (guard NEWCPU_H) has been included.  readcpu.h shares UAE_READCPU_H
 * with the vendored version so that guard is set on first inclusion.  */
#include "uae_cpu_2026/readcpu.h"
#include "uae_cpu_2026/newcpu.h"
#define UAE_NEWCPU_H 1

/* Override sysconfig.h's NATMEM_OFFSET=natmem_offset with MEMBaseDiff */
#ifdef NATMEM_OFFSET
#  undef NATMEM_OFFSET
#endif
#define NATMEM_OFFSET MEMBaseDiff

#endif /* PREVIOUS_UAE2026_VENDORED_PREAMBLE_H */
