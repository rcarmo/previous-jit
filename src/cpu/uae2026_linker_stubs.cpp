/*
 * uae2026_linker_stubs.cpp
 *
 * Runtime glue between the vendored UAE 2026 JIT compiler unit and
 * Previous's CPU/memory subsystem.
 *
 * NOTE: compemu_support.cpp (included via the compiler unity build) already
 * provides: jit_abort, jit_trace_add/pc_hit, flush_icache, m68k_do_compile_execute.
 * Do NOT redefine those here.
 */

#if defined(ENABLE_EXPERIMENTAL_UAE2026_JIT)

#include "uae2026_vendored_preamble.h"
#include "uae2026_compiler_prefs_shim.h"
#include "cpu_emulation.h"

/* ================================================================== *
 * A. C++ -> C forwarding wrappers                                      *
 * ================================================================== */

extern "C" {
    void    prev_MakeSR(void)                        __asm__("MakeSR");
    void    prev_MakeFromSR(void)                    __asm__("MakeFromSR");
    void    prev_Exception_1arg(int nr)              __asm__("Exception");
    void    prev_op_illg(uae_u32 opcode)             __asm__("op_illg");
    uae_u32 prev_get_disp_ea_020(uae_u32 b, int dp) __asm__("get_disp_ea_020");
    int     prev_intlev(void)                        __asm__("intlev");
    void    prev_m68k_reset(int hard)                __asm__("m68k_reset");
    void    prev_read_table68k(void)                 __asm__("read_table68k");
}

void MakeSR(void)               { prev_MakeSR(); }
void MakeFromSR(void)           { prev_MakeFromSR(); }
void Exception(int nr, uaecptr addr) { (void)addr; prev_Exception_1arg(nr); }
void REGPARAM2 op_illg(uae_u32 op) REGPARAM { prev_op_illg(op); }
uae_u32 REGPARAM2 get_disp_ea_020(uae_u32 base, uae_u32 dp) REGPARAM
                                { return prev_get_disp_ea_020(base, (int)dp); }
int  intlev(void)               { return prev_intlev(); }
void m68k_reset(void)           { prev_m68k_reset(0); }

/* init_table68k and exit_table68k are defined in uae2026_compiler_unit.cpp
 * via the vendored readcpu.cpp (with jit_table68k rename).              */
/* MEMBaseDiff: declared via memory.h (DIRECT_ADDRESSING). Provided by
 * the compiler unit as jit_MEMBaseDiff. Not redefined here.             */

/* ================================================================== *
 * B. JIT execution loop + specialties                                  *
 * m68k_do_compile_execute() is provided by compemu_support.cpp.        *
 * ================================================================== */

int m68k_do_specialties(void)
{
#define PREV_SPCFLAG_STOP       0x002
#define PREV_SPCFLAG_DOTRACE    0x080
#define PREV_SPCFLAG_DOINT      0x100
#define PREV_SPCFLAG_INT        0x008
#define PREV_SPCFLAG_BRK        0x010
#define PREV_SPCFLAG_MODE_CHG   0x800

    if (regs.spcflags & PREV_SPCFLAG_DOTRACE)
        Exception(9, 0);

    if (regs.spcflags & PREV_SPCFLAG_STOP) {
        MakeSR();
        return 1;
    }

    if (regs.spcflags & PREV_SPCFLAG_DOINT) {
        regs.spcflags &= ~PREV_SPCFLAG_DOINT;
        int intr = intlev();
        if (intr != -1 && intr > regs.intmask) {
            MakeSR();
            regs.stopped = 0;
            Exception(24 + intr, 0);
        }
    }

    if (regs.spcflags & PREV_SPCFLAG_INT) {
        regs.spcflags &= ~PREV_SPCFLAG_INT;
        regs.spcflags |= PREV_SPCFLAG_DOINT;
    }

    if (regs.spcflags & (PREV_SPCFLAG_BRK | PREV_SPCFLAG_MODE_CHG)) {
        regs.spcflags &= ~PREV_SPCFLAG_MODE_CHG;
        return 1;
    }

    return 0;
}

/* ================================================================== *
 * C. Residual stubs / storage                                          *
 * NOT defined here: flush_icache, jit_abort, jit_trace_add/pc_hit,    *
 *   m68k_do_compile_execute, InterruptFlags (all in compiler unit).    *
 * ================================================================== */

#include "uae_cpu_2026/fpu/core.h"
fpu_t fpu;

bool    UseJIT                       = false;
bool    tick_inhibit                 = false;
bool    basilisk_trace_after_table_ready = false;
uint16  emulated_ticks               = 0;

void cpu_do_check_ticks(void) {}
void jit_one_tick(void)       {}

void write_log(const TCHAR *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

int m68k_move2c(int reg, uae_u32 *val) { (void)reg; (void)val; return 0; }
int m68k_movec2(int reg, uae_u32 *val) { (void)reg; (void)val; return 0; }

#ifdef SleepAndWait
#  undef SleepAndWait
#endif
void SleepAndWait(void)
{
    struct timespec ts = { 0, 1000000 };
    nanosleep(&ts, NULL);
}

/* Prefs bridge */
bool PrefsFindBool(const char *name)
{
    if (__builtin_strcmp(name, "jit")           == 0) return true;
    if (__builtin_strcmp(name, "jit_fpu")       == 0) return Uae2026CompilerPrefsFPUEnabled();
    if (__builtin_strcmp(name, "jit_hardflush") == 0) return Uae2026CompilerPrefsHardFlushEnabled();
    if (__builtin_strcmp(name, "jit_constjump") == 0) return Uae2026CompilerPrefsConstJumpEnabled();
    return false;
}

int32_t PrefsFindInt32(const char *name)
{
    if (__builtin_strcmp(name, "jit_size")       == 0) return Uae2026CompilerPrefsCacheSizeKB();
    if (__builtin_strcmp(name, "jit_cache_size") == 0) return Uae2026CompilerPrefsCacheSizeKB();
    if (__builtin_strcmp(name, "special_mem")    == 0) return Uae2026CompilerPrefsSpecialMemDefault();
    return 0;
}

#endif /* ENABLE_EXPERIMENTAL_UAE2026_JIT */
