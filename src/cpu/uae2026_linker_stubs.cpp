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
    void    prev_doint(void)                         __asm__("doint");
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
        regs.spcflags |= PREV_SPCFLAG_INT;
    }

    if (regs.spcflags & PREV_SPCFLAG_INT) {
        static int lastintr = 0;
        int intr = intlev();
        regs.spcflags &= ~PREV_SPCFLAG_INT;
        if (intr != -1 && (intr > regs.intmask || (intr == 7 && intr > lastintr))) {
            MakeSR();
            regs.stopped = 0;
            regs.spcflags &= ~PREV_SPCFLAG_STOP;
            Exception(24 + intr, 0);
            regs.intmask = intr;
            prev_doint();
        }
        lastintr = intr;
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

extern "C" {
    uae_u32 Uae2026JitPhysGetByte(uae_u32 addr);
    uae_u32 Uae2026JitPhysGetWord(uae_u32 addr);
    uae_u32 Uae2026JitPhysGetLong(uae_u32 addr);
    void Uae2026JitPhysPutByte(uae_u32 addr, uae_u32 value);
    void Uae2026JitPhysPutWord(uae_u32 addr, uae_u32 value);
    void Uae2026JitPhysPutLong(uae_u32 addr, uae_u32 value);
    uae_u32 Uae2026JitMmuXlateData(uae_u32 addr);
    uae_u32 Uae2026JitMmuXlateCode(uae_u32 addr);
    uae_u32 Uae2026JitMmuGetByte(uae_u32 addr);
    uae_u32 Uae2026JitMmuGetWord(uae_u32 addr);
    uae_u32 Uae2026JitMmuGetLong(uae_u32 addr);
    void Uae2026JitMmuPutByte(uae_u32 addr, uae_u32 value);
    void Uae2026JitMmuPutWord(uae_u32 addr, uae_u32 value);
    void Uae2026JitMmuPutLong(uae_u32 addr, uae_u32 value);
}

static inline bool Uae2026JitRuntimeMmuEnabled(void)
{
    return regs.mmu_enabled != 0;
}

static uae_u32 Uae2026JitBankGetByte(uaecptr addr)
{
    return Uae2026JitRuntimeMmuEnabled() ? Uae2026JitMmuGetByte(addr) : Uae2026JitPhysGetByte(addr);
}

static uae_u32 Uae2026JitBankGetWord(uaecptr addr)
{
    return Uae2026JitRuntimeMmuEnabled() ? Uae2026JitMmuGetWord(addr) : Uae2026JitPhysGetWord(addr);
}

static uae_u32 Uae2026JitBankGetLong(uaecptr addr)
{
    return Uae2026JitRuntimeMmuEnabled() ? Uae2026JitMmuGetLong(addr) : Uae2026JitPhysGetLong(addr);
}

static void Uae2026JitBankPutByte(uaecptr addr, uae_u32 value)
{
    if (Uae2026JitRuntimeMmuEnabled())
        Uae2026JitMmuPutByte(addr, value);
    else
        Uae2026JitPhysPutByte(addr, value);
}

static void Uae2026JitBankPutWord(uaecptr addr, uae_u32 value)
{
    if (Uae2026JitRuntimeMmuEnabled())
        Uae2026JitMmuPutWord(addr, value);
    else
        Uae2026JitPhysPutWord(addr, value);
}

static void Uae2026JitBankPutLong(uaecptr addr, uae_u32 value)
{
    if (Uae2026JitRuntimeMmuEnabled())
        Uae2026JitMmuPutLong(addr, value);
    else
        Uae2026JitPhysPutLong(addr, value);
}

/* The vendored ARM64 memory-bank emitter indexes a bank structure by raw
 * pointer offsets: lget,wget,bget,lput,wput,bput,xlate/check...  Previous's
 * addrbank has no xlateaddr slot, so RAM/MMU mode gets a private compatible
 * bank table instead of pointing native code at Previous's physical banks. */
typedef uae_u32 (*Uae2026JitBankGetFunc)(uaecptr);
typedef void (*Uae2026JitBankPutFunc)(uaecptr, uae_u32);
typedef uintptr_t (*Uae2026JitBankXlateFunc)(uaecptr);
struct Uae2026JitBankCompat {
    Uae2026JitBankGetFunc lget, wget, bget;
    Uae2026JitBankPutFunc lput, wput, bput;
    Uae2026JitBankXlateFunc xlateaddr;
    Uae2026JitBankGetFunc check;
    int flags;
};

static void Uae2026JitSyncCodeRangeToShadow(uae_u32 addr, uae_u32 bytes)
{
    if (!jit_MEMBaseDiff || bytes == 0)
        return;
    const uae_u32 ram_base = 0x04000000u;
    const uae_u32 ram_size = 64u * 1024u * 1024u;
    const uae_u32 end = addr + bytes;
    if (end < addr || addr < ram_base || end > ram_base + ram_size)
        return;
    uae_u8 *shadow = (uae_u8 *)(jit_MEMBaseDiff + addr);
    uae_u32 i = 0;
    if (addr & 1u) {
        shadow[0] = (uae_u8)Uae2026JitPhysGetByte(addr);
        i = 1;
    }
    for (; i + 1 < bytes; i += 2) {
        const uae_u16 w = (uae_u16)Uae2026JitPhysGetWord(addr + i);
        shadow[i] = (uae_u8)(w >> 8);
        shadow[i + 1] = (uae_u8)w;
    }
    if (i < bytes)
        shadow[i] = (uae_u8)Uae2026JitPhysGetByte(addr + i);
}

extern "C" uintptr_t Uae2026JitMmuXlateCodeHost(uae_u32 addr)
{
    if (!jit_MEMBaseDiff)
        return 0;
    /* Always route through Uae2026JitMmuXlateCode(): with the 040 MMU disabled
     * it is an identity translation, but opcode-test forced code faults must
     * still be honored so code-host target materialization uses the same oracle
     * path as interpreter instruction fetches. */
    addr = Uae2026JitMmuXlateCode(addr);
    if (Uae2026JitRuntimeMmuEnabled()) {
        /* Code-space MMU translation returns a physical RAM address.  Keep the
         * executable JIT shadow coherent with the live memory map before exposing
         * the host pointer to execute_normal()/compiled branch targets; otherwise
         * low user virtual pages can execute stale ROM-overlay/probe bytes even
         * though the 040 code fetch translated to the correct physical page.
         * Use live addrbank reads rather than a raw NEXTRam memcpy because the
         * NeXT memory map can expose freshly generated low-code contents through
         * the active addrbank path before the raw shadow mirror is coherent. */
        const uae_u32 page_base = addr & ~0x1fffu;
        Uae2026JitSyncCodeRangeToShadow(page_base, 0x2000u);
    }
    return (uintptr_t)(jit_MEMBaseDiff + addr);
}

static uintptr_t Uae2026JitBankXlate(uaecptr addr)
{
    if (!jit_MEMBaseDiff)
        return 0;
    if (Uae2026JitRuntimeMmuEnabled())
        addr = Uae2026JitMmuXlateData(addr);
    return (uintptr_t)(jit_MEMBaseDiff + addr);
}

static Uae2026JitBankCompat uae2026_jit_mmu_bank = {
    Uae2026JitBankGetLong, Uae2026JitBankGetWord, Uae2026JitBankGetByte,
    Uae2026JitBankPutLong, Uae2026JitBankPutWord, Uae2026JitBankPutByte,
    Uae2026JitBankXlate, Uae2026JitBankGetByte, 0
};
static Uae2026JitBankCompat *uae2026_jit_mmu_banks[65536];

extern "C" uintptr_t Uae2026JitRamMmuBankTable(void)
{
    static bool initialized = false;
    if (!initialized) {
        for (unsigned i = 0; i < 65536; i++)
            uae2026_jit_mmu_banks[i] = &uae2026_jit_mmu_bank;
        initialized = true;
    }
    return (uintptr_t)uae2026_jit_mmu_banks;
}

static inline bool Uae2026JitVideoAliasRange(uae_u32 addr, uae_u32 bytes, uae_u32 *offset_out, int *mwf_out)
{
    const uae_u32 end = addr + bytes;
    if (end < addr)
        return false;
    for (uae_u32 base = 0x0b000000u; base <= 0x0f000000u; base += 0x01000000u) {
        if (addr >= base && end <= base + 0x00040000u) {
            if (offset_out)
                *offset_out = addr - base;
            if (mwf_out)
                *mwf_out = (base == 0x0b000000u) ? -1 : (int)((base >> 24) & 3u);
            return true;
        }
    }
    return false;
}

static inline uae_u32 Uae2026JitMwfApply(uae_u32 oldv, uae_u32 newv, int function, int size)
{
    static const uae_u8 mwf[4][4][4] = {
        { {0,0,0,0}, {0,0,1,1}, {0,1,1,2}, {0,1,2,3} },
        { {0,1,2,3}, {1,2,3,3}, {2,3,3,3}, {3,3,3,3} },
        { {0,0,0,0}, {1,1,0,0}, {2,1,1,0}, {3,2,1,0} },
        { {0,1,2,3}, {1,2,2,3}, {2,2,3,3}, {3,3,3,3} }
    };
    if (function < 0)
        return newv;
    uae_u32 v = 0;
    for (int i = 0; i < size * 4; i++) {
        const int a = (oldv >> (i * 2)) & 3;
        const int b = (newv >> (i * 2)) & 3;
        v |= (uae_u32)mwf[function & 3][a][b] << (i * 2);
    }
    return v;
}

extern "C" void Uae2026JitSyncVideoRangeToShadow(uae_u32 addr, uae_u32 bytes)
{
    extern uae_u8 NEXTVideo[];
    if (!jit_MEMBaseDiff || bytes == 0)
        return;
    uae_u32 off = 0;
    int mwf = -1;
    if (!Uae2026JitVideoAliasRange(addr, bytes, &off, &mwf))
        return;
    memcpy((void *)(jit_MEMBaseDiff + 0x0b000000u + off), NEXTVideo + off, bytes);
    memcpy((void *)(jit_MEMBaseDiff + 0x0c000000u + off), NEXTVideo + off, bytes);
    memcpy((void *)(jit_MEMBaseDiff + 0x0d000000u + off), NEXTVideo + off, bytes);
    memcpy((void *)(jit_MEMBaseDiff + 0x0e000000u + off), NEXTVideo + off, bytes);
    memcpy((void *)(jit_MEMBaseDiff + 0x0f000000u + off), NEXTVideo + off, bytes);
}

extern "C" void Uae2026JitSyncVideoToShadow(void)
{
    Uae2026JitSyncVideoRangeToShadow(0x0b000000u, 0x40000u);
}

extern "C" void Uae2026JitSyncVideoFromShadow(void)
{
    extern uae_u8 NEXTVideo[];
    if (!jit_MEMBaseDiff)
        return;
    static int full_sync_enabled = -1;
    if (full_sync_enabled < 0) {
        const char *env = getenv("PREVIOUS_JIT_FULL_VIDEO_FROM_SHADOW");
        full_sync_enabled = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    if (full_sync_enabled) {
        memcpy(NEXTVideo, (const void *)(jit_MEMBaseDiff + 0x0b000000u), 0x40000u);
        return;
    }
    if (regs.vbr < 0x0b000000u || regs.vbr >= 0x0b040000u)
        return;

    const uae_u32 off = regs.vbr - 0x0b000000u;
    if (off + 0x400u > 0x40000u)
        return;

    const uae_u8 *shadow = (const uae_u8 *)(jit_MEMBaseDiff + regs.vbr);
    /* Only seed an empty host-side vector table from shadow direct writes.
     * Do not mirror full VRAM from shadow: special-memory writes update
     * NEXTVideo directly and shadow can otherwise clobber valid host state. */
    if (do_get_mem_long((uae_u32 *)(NEXTVideo + off + 8)) == 0 &&
        do_get_mem_long((uae_u32 *)(uintptr_t)(shadow + 8)) != 0) {
        memcpy(NEXTVideo + off, shadow, 0x400);
        Uae2026JitSyncVideoToShadow();
    }
}

static inline bool Uae2026JitRamAliasRange(uae_u32 addr, uae_u32 bytes,
                                           uae_u32 *offset_out,
                                           uae_u32 *real_addr_out,
                                           int *mwf_out)
{
    const uae_u32 ram_base = 0x04000000u;
    const uae_u32 ram_size = 64u * 1024u * 1024u;
    if (bytes == 0)
        return false;
    const uae_u32 end = addr + bytes;
    if (end < addr)
        return false;

    int mwf = -1;
    uae_u32 off = 0;
    if (addr >= ram_base && end <= ram_base + ram_size) {
        off = addr - ram_base;
    } else if (addr >= 0x10000000u && end <= 0x20000000u) {
        mwf = (int)((addr >> 26) & 3u);
        off = addr & 0x03ffffffu;
        if (bytes > ram_size - off)
            return false;
    } else {
        return false;
    }

    if (offset_out)
        *offset_out = off;
    if (real_addr_out)
        *real_addr_out = ram_base + off;
    if (mwf_out)
        *mwf_out = mwf;
    return true;
}

static inline bool Uae2026JitRamRange(uae_u32 addr, uae_u32 bytes, uae_u32 *offset_out)
{
    int mwf = -1;
    if (!Uae2026JitRamAliasRange(addr, bytes, offset_out, nullptr, &mwf))
        return false;
    return mwf < 0;
}

extern "C" void Uae2026JitSyncRamRangeToShadow(uae_u32 addr, uae_u32 bytes)
{
    extern uae_u8 NEXTRam[];
    uae_u32 off = 0;
    uae_u32 real_addr = 0;
    if (!jit_MEMBaseDiff || !Uae2026JitRamAliasRange(addr, bytes, &off, &real_addr, nullptr))
        return;
    memcpy((void *)(jit_MEMBaseDiff + real_addr), NEXTRam + off, bytes);
}

extern "C" void Uae2026JitSyncRamToShadow(void)
{
    Uae2026JitSyncRamRangeToShadow(0x04000000u, 64u * 1024u * 1024u);
}

extern "C" uae_u32 Uae2026JitLiveGetByte(uae_u32 addr)
{
    return Uae2026JitPhysGetByte(addr);
}

extern "C" uae_u32 Uae2026JitLiveGetWord(uae_u32 addr)
{
    return Uae2026JitPhysGetWord(addr);
}

extern "C" uae_u32 Uae2026JitLiveGetLong(uae_u32 addr)
{
    return Uae2026JitPhysGetLong(addr);
}

extern "C" void Uae2026JitLivePutByte(uae_u32 addr, uae_u32 value)
{
    Uae2026JitPhysPutByte(addr, value);
    Uae2026JitSyncRamRangeToShadow(addr, 1);
    Uae2026JitSyncVideoRangeToShadow(addr, 1);
}

extern "C" void Uae2026JitLivePutWord(uae_u32 addr, uae_u32 value)
{
    Uae2026JitPhysPutWord(addr, value);
    Uae2026JitSyncRamRangeToShadow(addr, 2);
    Uae2026JitSyncVideoRangeToShadow(addr, 2);
}

extern "C" void Uae2026JitLivePutLong(uae_u32 addr, uae_u32 value)
{
    Uae2026JitPhysPutLong(addr, value);
    Uae2026JitSyncRamRangeToShadow(addr, 4);
    Uae2026JitSyncVideoRangeToShadow(addr, 4);
}

extern "C" void Uae2026JitFillBytes(uae_u32 addr, uae_u32 bytes, uae_u8 value)
{
    extern uae_u8 NEXTRam[];
    uae_u32 off = 0;
    uae_u32 real_addr = 0;
    int mwf = -1;
    if (Uae2026JitRamAliasRange(addr, bytes, &off, &real_addr, &mwf)) {
        if (mwf < 0) {
            memset(NEXTRam + off, value, bytes);
        } else {
            for (uae_u32 i = 0; i < bytes; i++)
                NEXTRam[off + i] = (uae_u8)Uae2026JitMwfApply(NEXTRam[off + i], value, mwf, 1);
        }
        if (jit_MEMBaseDiff)
            memcpy((void *)(jit_MEMBaseDiff + real_addr), NEXTRam + off, bytes);
        return;
    }

    extern uae_u8 NEXTVideo[];
    if (Uae2026JitVideoAliasRange(addr, bytes, &off, &mwf)) {
        if (mwf < 0) {
            memset(NEXTVideo + off, value, bytes);
        } else {
            for (uae_u32 i = 0; i < bytes; i++)
                NEXTVideo[off + i] = (uae_u8)Uae2026JitMwfApply(NEXTVideo[off + i], value, mwf, 1);
        }
        Uae2026JitSyncVideoRangeToShadow(addr, bytes);
    }
}

extern "C" void Uae2026JitFillLongs(uae_u32 addr, uae_u32 count, uae_u32 value)
{
    extern uae_u8 NEXTRam[];
    if (count > 0x3fffffffu)
        return;
    const uae_u32 bytes = count * 4u;
    uae_u32 off = 0;
    uae_u32 real_addr = 0;
    int mwf = -1;
    if (Uae2026JitRamAliasRange(addr, bytes, &off, &real_addr, &mwf)) {
        if (value == 0 && mwf < 0) {
            Uae2026JitFillBytes(addr, bytes, 0);
            return;
        }
        uae_u8 *ram = NEXTRam + off;
        uae_u8 *shadow = jit_MEMBaseDiff ? (uae_u8 *)(jit_MEMBaseDiff + real_addr) : nullptr;
        for (uae_u32 i = 0; i < count; i++) {
            const uae_u32 p = i * 4u;
            uae_u32 out = value;
            if (mwf >= 0) {
                const uae_u32 oldv = do_get_mem_long((uae_u32 *)(ram + p));
                out = Uae2026JitMwfApply(oldv, value, mwf, 4);
            }
            ram[p + 0] = (uae_u8)(out >> 24);
            ram[p + 1] = (uae_u8)(out >> 16);
            ram[p + 2] = (uae_u8)(out >> 8);
            ram[p + 3] = (uae_u8)out;
            if (shadow) {
                shadow[p + 0] = ram[p + 0];
                shadow[p + 1] = ram[p + 1];
                shadow[p + 2] = ram[p + 2];
                shadow[p + 3] = ram[p + 3];
            }
        }
        return;
    }

    extern uae_u8 NEXTVideo[];
    if (Uae2026JitVideoAliasRange(addr, bytes, &off, &mwf)) {
        for (uae_u32 i = 0; i < count; i++) {
            const uae_u32 p = off + i * 4u;
            uae_u32 out = value;
            if (mwf >= 0) {
                const uae_u32 oldv = do_get_mem_long((uae_u32 *)(NEXTVideo + p));
                out = Uae2026JitMwfApply(oldv, value, mwf, 4);
            }
            NEXTVideo[p + 0] = (uae_u8)(out >> 24);
            NEXTVideo[p + 1] = (uae_u8)(out >> 16);
            NEXTVideo[p + 2] = (uae_u8)(out >> 8);
            NEXTVideo[p + 3] = (uae_u8)out;
        }
        Uae2026JitSyncVideoRangeToShadow(addr, bytes);
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
    if (__builtin_strcmp(name, "jitfpu")        == 0) return Uae2026CompilerPrefsFPUEnabled();
    if (__builtin_strcmp(name, "jit_fpu")       == 0) return Uae2026CompilerPrefsFPUEnabled();
    if (__builtin_strcmp(name, "jitlazyflush")  == 0) return !Uae2026CompilerPrefsHardFlushEnabled();
    if (__builtin_strcmp(name, "jit_hardflush") == 0) return Uae2026CompilerPrefsHardFlushEnabled();
    if (__builtin_strcmp(name, "jitinline")     == 0) return Uae2026CompilerPrefsConstJumpEnabled();
    if (__builtin_strcmp(name, "jit_constjump") == 0) return Uae2026CompilerPrefsConstJumpEnabled();
    return false;
}

int32_t PrefsFindInt32(const char *name)
{
    if (__builtin_strcmp(name, "jitcachesize")   == 0) return Uae2026CompilerPrefsCacheSizeKB();
    if (__builtin_strcmp(name, "jit_size")       == 0) return Uae2026CompilerPrefsCacheSizeKB();
    if (__builtin_strcmp(name, "jit_cache_size") == 0) return Uae2026CompilerPrefsCacheSizeKB();
    if (__builtin_strcmp(name, "special_mem")    == 0) return Uae2026CompilerPrefsSpecialMemDefault();
    /* "cpu" was previously unhandled and returned 0, which sync_jit_prefs
     * fed through pref_cpu_to_model(0) -> 68000.  That made compile_block's
     * `currprefs.cpu_model >= 68020` gate fail unconditionally and the JIT
     * silently fell back to interpreter dispatch on every single block.
     * Default to 68040 (the NeXT cube model) so the compiler runs. */
    if (__builtin_strcmp(name, "cpu")            == 0) return 4; /* 68040 */
    return 0;
}

#endif /* ENABLE_EXPERIMENTAL_UAE2026_JIT */
// added for one-shot RTE handoff resume
extern "C" unsigned long Uae2026JitInterpResumeCountdown = 0;
