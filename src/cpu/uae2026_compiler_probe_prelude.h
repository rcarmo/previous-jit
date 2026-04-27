#ifndef PREVIOUS_UAE2026_COMPILER_PROBE_PRELUDE_H
#define PREVIOUS_UAE2026_COMPILER_PROBE_PRELUDE_H

/*
 * Compile-time probe compatibility shim for the vendored UAE 2026 compiler.
 *
 * This header is for syntax probing only. It does not change the emulator
 * runtime; it just supplies enough declarations and aliases to measure which
 * integration blockers remain when we try to compile the vendored compiler
 * core inside Previous.
 */

#include <stdint.h>
#include <stddef.h>

#include "uae_cpu_2026/sysdeps.h"
#include "uae_cpu_2026/m68k.h"
#include "uae_cpu_2026/readcpu.h"

#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE inline __attribute__((always_inline))
#endif

#ifndef PREVIOUS_UAE2026_PROBE_FLAG_STRUCT_DEFINED
#define PREVIOUS_UAE2026_PROBE_FLAG_STRUCT_DEFINED 1
struct flag_struct {
    uae_u32 nzcv;
    uae_u32 x;
};
#endif
typedef struct flag_struct flag_struct;
extern struct flag_struct regflags;

#ifndef FLAGBIT_N
#define FLAGBIT_N 31
#endif
#ifndef FLAGBIT_Z
#define FLAGBIT_Z 30
#endif
#ifndef FLAGBIT_C
#define FLAGBIT_C 29
#endif
#ifndef FLAGVAL_N
#define FLAGVAL_N (1u << FLAGBIT_N)
#endif
#ifndef FLAGVAL_Z
#define FLAGVAL_Z (1u << FLAGBIT_Z)
#endif
#ifndef FLAGVAL_C
#define FLAGVAL_C (1u << FLAGBIT_C)
#endif
#ifndef CLEAR_CZNV
#define CLEAR_CZNV() (regflags.nzcv = 0)
#endif
#ifndef SET_ZFLG
#define SET_ZFLG(y) (regflags.nzcv = (regflags.nzcv & ~FLAGVAL_Z) | (((y) ? 1u : 0u) << FLAGBIT_Z))
#endif
#ifndef SET_NFLG
#define SET_NFLG(y) (regflags.nzcv = (regflags.nzcv & ~FLAGVAL_N) | (((y) ? 1u : 0u) << FLAGBIT_N))
#endif

#ifndef uint8
typedef uae_u8 uint8;
#endif
#ifndef uint16
typedef uae_u16 uint16;
#endif
#ifndef uint32
typedef uae_u32 uint32;
#endif
#ifndef int32
typedef uae_s32 int32;
#endif
#ifndef uint64
typedef uae_u64 uint64;
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifndef do_byteswap_16
#define do_byteswap_16(x) __builtin_bswap16((uae_u16)(x))
#endif
#ifndef do_byteswap_32
#define do_byteswap_32(x) __builtin_bswap32((uae_u32)(x))
#endif

extern uintptr_t MEMBaseDiff;
extern uintptr_t natmem_offset;
extern volatile uae_u32 InterruptFlags;
extern int32 jit_countdown;
extern uae_u8 *RAMBaseHost;
extern uae_u8 *ROMBaseHost;
extern uae_u32 RAMSize;
extern uae_u32 ROMSize;
extern uae_u32 ROMBaseMac;
extern void *specflags;

struct previous_probe_addrbank {
    uintptr_t baseaddr;
};
extern struct previous_probe_addrbank kickmem_bank;
extern struct previous_probe_addrbank rtarea_bank;

extern uae_u32 get_byte(uaecptr addr);
extern uae_u32 get_word(uaecptr addr);
extern uae_u32 get_long(uaecptr addr);
extern void put_byte(uaecptr addr, uae_u32 value);
extern void put_word(uaecptr addr, uae_u32 value);
extern void put_long(uaecptr addr, uae_u32 value);
extern uae_u32 get_iword(int offset);
extern uae_u32 get_ilong(int offset);
extern uae_u32 get_ibyte(int offset);
extern uae_u32 mmu_get_byte(uaecptr addr, int flags, int size);
extern uae_u32 mmu_get_word(uaecptr addr, int flags, int size);
extern uae_u32 mmu_get_long(uaecptr addr, int flags, int size);
extern uae_u32 mmu_get_long_unaligned(uaecptr addr, int flags);
static ALWAYS_INLINE bool is_unaligned(uaecptr addr, int size)
{
    return (addr & (size - 1)) != 0;
}
extern uae_u32 do_get_mem_byte(const uae_u8 *ptr);
extern uae_u32 do_get_mem_word(const uae_u16 *ptr);
extern uae_u32 do_get_mem_long(const uae_u32 *ptr);
extern uae_u8 *get_real_address(uaecptr addr, int write, int size);
static ALWAYS_INLINE uae_u8 *get_real_address(uaecptr addr)
{
    return get_real_address(addr, 0, 0);
}
extern uae_u32 get_virtual_address(uae_u8 *addr);
extern void cpu_do_check_ticks(void);

#endif
