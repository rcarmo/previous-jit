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
            regs.spcflags &= ~PREV_SPCFLAG_STOP;
            Exception(24 + intr, 0);
            regs.intmask = intr;
            regs.spcflags |= PREV_SPCFLAG_INT;
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

extern "C" void Uae2026JitCpuCheckTicks(int cycles);
extern uintptr_t jit_MEMBaseDiff;

static inline bool Uae2026JitRamRange(uae_u32 addr, uae_u32 bytes, uae_u32 *offset_out)
{
    const uae_u32 ram_base = 0x04000000u;
    const uae_u32 ram_size = 64u * 1024u * 1024u;
    if (addr < ram_base || addr >= ram_base + ram_size)
        return false;
    const uae_u32 off = addr - ram_base;
    if (bytes > ram_size - off)
        return false;
    if (offset_out)
        *offset_out = off;
    return true;
}

extern "C" void Uae2026JitSyncRamRangeToShadow(uae_u32 addr, uae_u32 bytes)
{
    extern uae_u8 NEXTRam[];
    uae_u32 off = 0;
    if (!jit_MEMBaseDiff || !Uae2026JitRamRange(addr, bytes, &off))
        return;
    memcpy((void *)(jit_MEMBaseDiff + addr), NEXTRam + off, bytes);
}

extern "C" void Uae2026JitSyncRamToShadow(void)
{
    Uae2026JitSyncRamRangeToShadow(0x04000000u, 64u * 1024u * 1024u);
}

extern "C" void Uae2026JitFillBytes(uae_u32 addr, uae_u32 bytes, uae_u8 value)
{
    extern uae_u8 NEXTRam[];
    uae_u32 off = 0;
    if (!Uae2026JitRamRange(addr, bytes, &off))
        return;
    memset(NEXTRam + off, value, bytes);
    if (jit_MEMBaseDiff)
        memset((void *)(jit_MEMBaseDiff + addr), value, bytes);
}

extern "C" void Uae2026JitFillLongs(uae_u32 addr, uae_u32 count, uae_u32 value)
{
    extern uae_u8 NEXTRam[];
    if (count > 0x3fffffffu)
        return;
    const uae_u32 bytes = count * 4u;
    uae_u32 off = 0;
    if (!Uae2026JitRamRange(addr, bytes, &off))
        return;

    uae_u8 *ram = NEXTRam + off;
    uae_u8 *shadow = jit_MEMBaseDiff ? (uae_u8 *)(jit_MEMBaseDiff + addr) : nullptr;
    if (value == 0) {
        Uae2026JitFillBytes(addr, bytes, 0);
        return;
    }

    for (uae_u32 i = 0; i < count; i++) {
        const uae_u32 p = i * 4u;
        ram[p + 0] = (uae_u8)(value >> 24);
        ram[p + 1] = (uae_u8)(value >> 16);
        ram[p + 2] = (uae_u8)(value >> 8);
        ram[p + 3] = (uae_u8)value;
        if (shadow) {
            shadow[p + 0] = ram[p + 0];
            shadow[p + 1] = ram[p + 1];
            shadow[p + 2] = ram[p + 2];
            shadow[p + 3] = ram[p + 3];
        }
    }
}

extern "C" void Uae2026JitFastClearLongs(uae_u32 addr, uae_u32 count)
{
    Uae2026JitFillLongs(addr, count, 0);
}

extern "C" void Uae2026JitFastClearBytes(uae_u32 addr, uae_u32 count)
{
    Uae2026JitFillBytes(addr, count, 0);
}

void cpu_do_check_ticks(void)
{
    Uae2026JitCpuCheckTicks(10000000 / 512);
}

void jit_one_tick(void)
{
    cpu_do_check_ticks();
}

void write_log(const TCHAR *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

extern "C" void mmu_set_tc(uae_u16 tc);
extern "C" void mmu_tt_modified(void);
extern "C" void set_cpu_caches(bool flush);
int m68k_move2c(int reg, uae_u32 *val)
{
    switch (reg) {
    case 0: regs.sfc = *val & 7; break;
    case 1: regs.dfc = *val & 7; break;
    case 2:
        regs.cacr = *val & 0x80008000u;
        set_cpu_caches(false);
        break;
    case 3:
        regs.tc = *val & 0xc000u;
        mmu_set_tc((uae_u16)regs.tc);
        break;
    case 4: regs.itt0 = *val & 0xffffe364u; mmu_tt_modified(); break;
    case 5: regs.itt1 = *val & 0xffffe364u; mmu_tt_modified(); break;
    case 6: regs.dtt0 = *val & 0xffffe364u; mmu_tt_modified(); break;
    case 7: regs.dtt1 = *val & 0xffffe364u; mmu_tt_modified(); break;
    case 8: break;
    case 0x800: regs.usp = *val; break;
    case 0x801: regs.vbr = *val; break;
    case 0x802: regs.caar = *val; break;
    case 0x803: regs.msp = *val; if (regs.m == 1) regs.regs[15] = regs.msp; break;
    case 0x804: regs.isp = *val; if (regs.m == 0) regs.regs[15] = regs.isp; break;
    case 0x805: regs.mmusr = *val; break;
    case 0x806: regs.urp = *val & 0xfffffe00u; break;
    case 0x807: regs.srp = *val & 0xfffffe00u; break;
    case 0x808: break;
    default:
        op_illg(0x4e7b);
        return 0;
    }
    return 1;
}

int m68k_movec2(int reg, uae_u32 *val)
{
    switch (reg) {
    case 0: *val = regs.sfc; break;
    case 1: *val = regs.dfc; break;
    case 2: *val = regs.cacr & 0x80008000u; break;
    case 3: *val = regs.tc; break;
    case 4: *val = regs.itt0; break;
    case 5: *val = regs.itt1; break;
    case 6: *val = regs.dtt0; break;
    case 7: *val = regs.dtt1; break;
    case 8: *val = 0; break;
    case 0x800: *val = regs.usp; break;
    case 0x801: *val = regs.vbr; break;
    case 0x802: *val = regs.caar; break;
    case 0x803: *val = regs.m == 1 ? regs.regs[15] : regs.msp; break;
    case 0x804: *val = regs.m == 0 ? regs.regs[15] : regs.isp; break;
    case 0x805: *val = regs.mmusr; break;
    case 0x806: *val = regs.urp; break;
    case 0x807: *val = regs.srp; break;
    case 0x808: *val = 0; break;
    default:
        op_illg(0x4e7a);
        return 0;
    }
    return 1;
}

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
