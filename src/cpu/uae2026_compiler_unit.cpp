/*
 * uae2026_compiler_unit.cpp
 *
 * UAE 2026 ARM64 JIT compiler bring-up unit for Previous.
 *
 * The JIT uses 'regs' as the name for the CPU register struct (from
 * uae_cpu_2026/registers.h).  Previous's CPU code also uses 'regs' but
 * with a different struct layout.  Both refer to the SAME symbol — the
 * linker picks the largest definition.
 *
 * To make the JIT fields (mem_banks, cache_tags, scratchregs, jit_fpregs…)
 * visible at the correct offsets, we add those fields to Previous's
 * newcpu.h struct at the right position.  The regflags, MEMBaseDiff, and
 * vendored compiler prefs symbols are renamed so they don't conflict with
 * Previous's native versions.
 */

#if defined(ENABLE_EXPERIMENTAL_UAE2026_JIT)

/* Pulled in early so set_status()'s diagnostic abort can dump a backtrace. */
#include <execinfo.h>
#include <cstddef>

/* Rename vendored globals to avoid collisions with Previous-native globals. */
#define regflags      jit_regflags
#define MEMBaseDiff   jit_MEMBaseDiff
#define currprefs     uae2026_currprefs
#define changed_prefs uae2026_changed_prefs

/* ------------------------------------------------------------------ *
 * Shared compatibility preamble                                        *
 * ------------------------------------------------------------------ */
#include "uae2026_vendored_preamble.h"

static_assert(offsetof(struct regstruct, scratchregs) == 196, "vendored scratchregs ABI");
static_assert(offsetof(struct regstruct, jit_scratch_vregs) == 216, "vendored scratch spill ABI");
static_assert(offsetof(struct regstruct, jit_exception_oldpc) == 284, "vendored exception-PC ABI");
static_assert(offsetof(struct regstruct, mem_banks) == 400, "vendored bank-table ABI");
static_assert(offsetof(struct regstruct, cache_tags) == 408, "vendored cache-tag ABI");

/* ------------------------------------------------------------------ *
 * jit_regflags storage (renamed from regflags)                        *
 * ------------------------------------------------------------------ */
struct flag_struct jit_regflags __asm__("jit_regflags");

/* ------------------------------------------------------------------ *
 * jit_MEMBaseDiff storage (renamed from MEMBaseDiff)                  *
 * ------------------------------------------------------------------ */
uintptr jit_MEMBaseDiff = 0;

/* ------------------------------------------------------------------ *
 * CPU emulation interface stubs                                        *
 * ------------------------------------------------------------------ */
uint32  RAMBaseMac        = 0;
uint8  *RAMBaseHost       = nullptr;
uint32  RAMSize           = 0;
uint32  ROMBaseMac        = 0;
uint8  *ROMBaseHost       = nullptr;
uint32  ROMSize           = 0;
uint8  *MacFrameBaseHost  = nullptr;
uint32  MacFrameSize      = 0;
int     MacFrameLayout    = 0;
uint32  InterruptFlags    = 0;

/* ------------------------------------------------------------------ *
 * Basilisk ROM bank stubs                                              *
 * ------------------------------------------------------------------ */
struct _addrbank_stub { uae_u8 *baseaddr; uae_u32 allocated_size; };
static struct _addrbank_stub kickmem_bank = { nullptr, 0 };
static struct _addrbank_stub rtarea_bank  = { nullptr, 0 };

/* ------------------------------------------------------------------ *
 * Vendored opcode table + readcpu (with renamed globals)               *
 * Provides init_table68k(), exit_table68k(), jit_table68k.             *
 * ------------------------------------------------------------------ */
#define table68k          jit_table68k
#define lookuptab         jit_lookuptab
#define do_merges         jit_do_merges
#define get_no_mismatches jit_get_no_mismatches
#define nr_cpuop_funcs    jit_nr_cpuop_funcs
#define defs68k           jit_defs68k
#define n_defs68k         jit_n_defs68k
#include "uae_cpu_2026/cpudefs_jit.c"
#include "uae_cpu_2026/readcpu.cpp"

/* ------------------------------------------------------------------ *
 * compemu_support.cpp (unity root — includes all JIT support files)   *
 * ------------------------------------------------------------------ */
#include "uae_cpu_2026/compiler/compemu_support.cpp"

/* ------------------------------------------------------------------ *
 * compemu.cpp — M68K opcode JIT handlers (all 8 PART_N)              *
 * ------------------------------------------------------------------ */
#define NATIVE_FLAGS_X86_H  /* block x86 flags; ARM64 uses flags_arm.h */
#define PART_1 1
#define PART_2 1
#define PART_3 1
#define PART_4 1
#define PART_5 1
#define PART_6 1
#define PART_7 1
#define PART_8 1
#include "uae_cpu_2026/compiler/compemu.cpp"

/* ------------------------------------------------------------------ *
 * compstbl_arm.cpp — op_smalltbl_0_comp_ff/nf dispatch tables         *
 * ------------------------------------------------------------------ */
#include "uae_cpu_2026/compiler/compstbl_arm.cpp"


/* ------------------------------------------------------------------ *
 * C ABI wrapper so the bridge can hard-flush the JIT translation     *
 * cache from outside this translation unit (e.g. when resuming JIT   *
 * after a one-shot interpreter handoff). flush_icache_hard() itself  *
 * is static inside compemu_support.cpp and collides with a no-op     *
 * macro in Previous's newcpu.h, so we expose it under a fresh name.  *
 * ------------------------------------------------------------------ */
#ifdef flush_icache_hard
#undef flush_icache_hard
#endif
extern "C" void Uae2026CompilerFlushCacheHard(void)
{
    flush_icache_hard(3);
}

/* Lazy counterpart. An MMU ATC flush does not by itself invalidate translated
 * code: block dispatch is keyed on the freshly translated host code pointer and
 * every non-direct handler re-verifies regs.pc_p before running, so a remapped
 * logical page can no longer reach its old block. Marking blocks for checksum
 * re-verification is therefore sufficient, and avoids discarding the entire
 * translation cache on every PFLUSH (NeXTSTEP Mach issues ~1.5k/s). */
extern "C" void Uae2026CompilerFlushCacheLazy(void)
{
    extern void Uae2026CompilerFlushCacheLazyImpl(void);
    Uae2026CompilerFlushCacheLazyImpl();
}

#endif /* ENABLE_EXPERIMENTAL_UAE2026_JIT */
