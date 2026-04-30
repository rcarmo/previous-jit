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
 * newcpu.h struct at the right position.  The regflags and MEMBaseDiff
 * symbols are renamed so they don't conflict with Previous's versions.
 */

#if defined(ENABLE_EXPERIMENTAL_UAE2026_JIT)

/* Rename regflags and MEMBaseDiff to avoid collision with Previous's globals. */
#define regflags    jit_regflags
#define MEMBaseDiff jit_MEMBaseDiff

/* ------------------------------------------------------------------ *
 * Shared compatibility preamble                                        *
 * ------------------------------------------------------------------ */
#include "uae2026_vendored_preamble.h"

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
#include "uae_cpu_2026/compiler/compemu_b2_gapfill.cpp"
#include "uae_cpu_2026/compiler/compemu_priority_gapfill.cpp"
#include "uae_cpu_2026/compiler/compemu_moves_gapfill.cpp"
#include "uae_cpu_2026/compiler/compemu_pack_gapfill.cpp"
#include "uae_cpu_2026/compiler/compemu_bitfield_gapfill.cpp"
#include "uae_cpu_2026/compiler/compemu_trapcc_gapfill.cpp"
#include "uae_cpu_2026/compiler/compemu_chk2_cas_gapfill.cpp"
#include "uae_cpu_2026/compiler/compemu_next5_gapfill.cpp"

/* ------------------------------------------------------------------ *
 * compstbl_arm.cpp — op_smalltbl_0_comp_ff/nf dispatch tables         *
 * ------------------------------------------------------------------ */
#include "uae_cpu_2026/compiler/compstbl_arm.cpp"

#endif /* ENABLE_EXPERIMENTAL_UAE2026_JIT */
