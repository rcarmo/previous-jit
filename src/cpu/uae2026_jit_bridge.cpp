#include "uae2026_jit_bridge.h"

#if defined(ENABLE_EXPERIMENTAL_UAE2026_JIT)

/* The bridge uses only Previous's headers (newcpu.h etc.).
 * 'regs' in this TU is the same shared symbol as the JIT's 'regs'.
 * We added the JIT fields (mem_banks, cache_tags, etc.) to newcpu.h  *
 * at the correct offsets so both compilation units agree on layout.   */
#include "sysdeps.h"
#include "memory.h"
#include "newcpu.h"
#include "options_cpu.h"
#include "uae2026_compiler_prefs_shim.h"
#include "uae_cpu_2026/compiler/jit_native_helpers.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <csetjmp>
#include <time.h>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

/* Vendored JIT entry points (C++ linkage, defined in uae2026_compiler_unit.cpp) */
extern void compiler_init(void);
extern void compiler_exit(void);
extern void build_comp(void);
extern void alloc_cache(void);
extern void compemu_reset(void);

/* JIT execute loop (defined in uae2026_linker_stubs.cpp) */
extern void m68k_do_compile_execute(void);
extern "C" {
    extern jmp_buf __exbuf;
    extern int __exvalue;
    jmp_buf *__poptry(void);
    void __pushtry(jmp_buf *j);
    int __is_catched(void);
    void prev_Exception_1arg(int nr) __asm__("Exception");
    void prev_m68k_reset(int hard) __asm__("m68k_reset");
}
extern bool mmu_restart;
extern uae_u16 mmu_opcode;

/* UseJIT flag (defined in uae2026_linker_stubs.cpp) */
extern bool UseJIT;

/* MEMBaseDiff -- host base offset for the JIT's direct-addressing  */
extern uintptr_t jit_MEMBaseDiff;
extern "C" uintptr_t Uae2026JitRamMmuBankTable(void);
extern "C" void Uae2026JitSyncRamToShadow(void);
extern "C" uae_u32 Uae2026JitLiveGetWord(uae_u32 addr);
extern "C" uae_u32 Uae2026JitLastInstructionPc;
extern "C" uae_u32 Uae2026JitLastSr;
extern "C" uae_u32 Uae2026JitLastA7;
extern "C" uae_u32 Uae2026JitLastExceptionSp;
extern "C" struct flag_struct Uae2026JitLastFlags;

/* regflags — CPU flag struct (Previous layout: cznv field). */
extern struct flag_struct regflags;

/* jit_regflags — the JIT's AArch64 flag struct (nzcv/x).      *
 * Defined in uae2026_compiler_unit.cpp as a renamed 'regflags'. */
struct jit_flag_struct { uae_u32 nzcv; uae_u32 x; };
extern struct jit_flag_struct jit_regflags;

namespace {
static bool bridge_logged = false;
static char bridge_summary[768];
static bool bootstrap_attempted = false;
static bool bootstrap_active = false;
static bool compiler_prefs_applied = false;
static bool compiler_initialized = false;
static bool jit_active = false;
static unsigned long block_exit_request_count = 0;
static uae_u8 *jit_shadow_base = nullptr;
static size_t jit_shadow_size = 0;
static void *bootstrap_cache = nullptr;
static size_t bootstrap_cache_bytes = 0;

static uae_u32 bridge_live_canonical_addr(uae_u32 addr)
{
    if (addr >= 0x10000000u && addr < 0x20000000u)
        return 0x04000000u | (addr & 0x03ffffffu);
    if (addr >= 0x0c000000u && addr < 0x10000000u)
        return 0x0b000000u | (addr & 0x0003ffffu);
    return addr;
}

static bool bridge_live_readable(uae_u32 addr, uae_u32 bytes)
{
    if (bytes == 0)
        return false;
    addr = bridge_live_canonical_addr(addr);
    const uae_u32 end = addr + bytes;
    if (end < addr)
        return false;
    return (addr >= 0x01000000u && end <= 0x01020000u) ||
           (addr >= 0x04000000u && end <= 0x08000000u) ||
           (addr >= 0x0b000000u && end <= 0x0b040000u) ||
           (addr < 0x00040000u && end <= 0x00040000u);
}

static uae_u32 bridge_live_peek_word(uae_u32 addr)
{
    addr = bridge_live_canonical_addr(addr);
    return bridge_live_readable(addr, 2) ? Uae2026JitLiveGetWord(addr) : 0;
}

static uae_u32 bridge_live_peek_long(uae_u32 addr)
{
    return bridge_live_readable(addr, 4) ? bridge_live_peek_word(addr) << 16 | bridge_live_peek_word(addr + 2) : 0;
}

static bool bridge_shadow_host_readable(const uae_u8 *host, size_t bytes)
{
    if (!jit_shadow_base || !host || bytes == 0)
        return false;
    const uintptr_t start = (uintptr_t)host;
    const uintptr_t base = (uintptr_t)jit_shadow_base;
    if (start < base)
        return false;
    const uintptr_t off = start - base;
    return off <= jit_shadow_size && bytes <= jit_shadow_size - off;
}

static uae_u32 bridge_shadow_host_peek_word(const uae_u8 *host)
{
    return bridge_shadow_host_readable(host, 2) ? ((uae_u32)host[0] << 8) | host[1] : 0xffffu;
}

static void sync_shadow_video(void)
{
    if (!jit_shadow_base || jit_shadow_size < 0x10040000UL)
        return;
    memcpy(jit_shadow_base + 0x0b000000, NEXTVideo, 0x40000);
    memcpy(jit_shadow_base + 0x0c000000, NEXTVideo, 0x40000);
    memcpy(jit_shadow_base + 0x0d000000, NEXTVideo, 0x40000);
    memcpy(jit_shadow_base + 0x0e000000, NEXTVideo, 0x40000);
    memcpy(jit_shadow_base + 0x0f000000, NEXTVideo, 0x40000);
}

struct previous_uae2026_prefs {
    bool requested;
    bool bootstrap_enabled;
    bool aslr_active;
    bool host_supported;
    bool runtime_disabled;
    bool bootstrap_ready;
    int cachesize_kb;
    bool jit_fpu;
    bool lazy_flush;
    bool const_jump;
    int cpu_model;
    int cpu_level;
    int mmu_model;
    int fpu_model;
    int fpu_revision;
    bool cpu_compatible;
    bool fpu_strict;
    bool compiler_prefs_applied;
};

static const char *host_arch()
{
#if defined(__aarch64__)
    return "aarch64";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__arm__)
    return "arm";
#else
    return "other";
#endif
}

static bool env_truthy(const char *name, bool fallback)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return fallback;
    if (!strcasecmp(value, "1") || !strcasecmp(value, "true") ||
        !strcasecmp(value, "yes") || !strcasecmp(value, "on"))
        return true;
    if (!strcasecmp(value, "0") || !strcasecmp(value, "false") ||
        !strcasecmp(value, "no") || !strcasecmp(value, "off"))
        return false;
    return fallback;
}

static int env_int(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return fallback;
    return atoi(value);
}

static unsigned long env_ulong(const char *name, unsigned long fallback)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return fallback;
    return strtoul(value, nullptr, 0);
}

static void bridge_trace_lowpc_resume(const char *phase, int prb)
{
    if (!env_truthy("B2_JIT_TRACE_LOWPC_RESUME", false) || prb != 2 || regs.fault_pc >= 0x00020000u)
        return;

    static unsigned long count = 0;
    const unsigned long limit = env_ulong("B2_JIT_TRACE_LOWPC_RESUME_LIMIT", 128);
    if (count >= limit)
        return;
    const unsigned long n = count++;
    const uae_u32 pc = m68k_getpc();
    const uae_u32 sp = m68k_areg(regs, 7);
    fprintf(stderr,
            "JIT_LOWPC_RESUME %s n=%lu prb=%d pc=%08x fault_pc=%08x addr=%08x op=%04x ext=%04x mmu_opcode=%04x mmu_restart=%d "
            "sr=%04x s=%d m=%d vbr=%08x a7=%08x usp=%08x isp=%08x msp=%08x spc=%08x "
            "d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x "
            "a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x "
            "lastpc=%08x lastsr=%08x lasta7=%08x lastexcsp=%08x lastflags=%08x/%08x jitflags=%08x/%08x "
            "spm4=%08x sp0=%08x sp4=%08x sp8=%08x fr_sr=%04x fr_pc=%08x fr_vec=%04x fr8=%08x fr12=%08x\n",
            phase, n, prb, (unsigned)pc, (unsigned)regs.fault_pc,
            (unsigned)regs.mmu_fault_addr,
            (unsigned)bridge_live_peek_word(regs.fault_pc),
            (unsigned)bridge_live_peek_word(regs.fault_pc + 2),
            (unsigned)(uae_u16)mmu_opcode, (int)mmu_restart,
            (unsigned)regs.sr, (int)regs.s, (int)regs.m, (unsigned)regs.vbr,
            (unsigned)sp, (unsigned)regs.usp, (unsigned)regs.isp,
            (unsigned)regs.msp, (unsigned)regs.spcflags,
            (unsigned)regs.regs[0], (unsigned)regs.regs[1],
            (unsigned)regs.regs[2], (unsigned)regs.regs[3],
            (unsigned)regs.regs[4], (unsigned)regs.regs[5],
            (unsigned)regs.regs[6], (unsigned)regs.regs[7],
            (unsigned)regs.regs[8], (unsigned)regs.regs[9],
            (unsigned)regs.regs[10], (unsigned)regs.regs[11],
            (unsigned)regs.regs[12], (unsigned)regs.regs[13],
            (unsigned)regs.regs[14],
            (unsigned)Uae2026JitLastInstructionPc,
            (unsigned)Uae2026JitLastSr, (unsigned)Uae2026JitLastA7,
            (unsigned)Uae2026JitLastExceptionSp,
            (unsigned)Uae2026JitLastFlags.cznv, (unsigned)Uae2026JitLastFlags.x,
            (unsigned)jit_regflags.nzcv, (unsigned)jit_regflags.x,
            bridge_live_peek_long(sp >= 4 ? sp - 4 : sp),
            bridge_live_peek_long(sp), bridge_live_peek_long(sp + 4),
            bridge_live_peek_long(sp + 8), bridge_live_peek_word(sp),
            bridge_live_peek_long(sp + 2), bridge_live_peek_word(sp + 6),
            bridge_live_peek_long(sp + 8), bridge_live_peek_long(sp + 12));
}

static int bridge_move_size_increment(uae_u16 opcode, int areg)
{
    const uae_u16 family = opcode & 0x3000u;
    if (family == 0x1000u)
        return areg_byteinc[areg & 7];
    if (family == 0x2000u)
        return 4;
    if (family == 0x3000u)
        return 2;
    return 0;
}

static void bridge_set_active_a7(uae_u32 value)
{
    m68k_areg(regs, 7) = value;
    if (!regs.s)
        regs.usp = value;
    else if (regs.m)
        regs.msp = value;
    else
        regs.isp = value;
}

enum class bridge_mmu_txn_kind : uae_u32 {
    none = 0,
    call_push = 1,
    return_pop = 2,
};

struct bridge_mmu_txn {
    bridge_mmu_txn_kind kind = bridge_mmu_txn_kind::none;
    uae_u32 pc = 0;
    uae_u16 opcode = 0;
    uae_u32 pre_sr = 0;
    uae_u32 pre_a7 = 0;
    uae_u32 side_old = 0;
    uae_u32 side_new = 0;
    uae_u32 aux0 = 0;
    uae_u32 aux1 = 0;
};

static bridge_mmu_txn bridge_active_mmu_txn;

static void bridge_clear_mmu_txn()
{
    bridge_active_mmu_txn = bridge_mmu_txn{};
}

static bool bridge_rollback_mmu_txn(uae_u32 fault_pc)
{
    if (bridge_active_mmu_txn.kind == bridge_mmu_txn_kind::none)
        return false;

    const bridge_mmu_txn txn = bridge_active_mmu_txn;
    bridge_clear_mmu_txn();
    switch (txn.kind) {
        case bridge_mmu_txn_kind::call_push: {
            const bool canonicalize_target_fetch = txn.aux1 && txn.pc == 0x05027706u;
            if (canonicalize_target_fetch && regs.mmu_fault_addr != txn.aux1) {
                if (getenv("B2_JIT_TRACE_CALL_ROLLBACK")) {
                    fprintf(stderr,
                            "JIT_CALL_TARGET_ROLLBACK_TXN_MISS fault_pc=%08x op_pc=%08x op=%04x addr=%08x target=%08x sp=%08x\n",
                            (unsigned)fault_pc, (unsigned)txn.pc, (unsigned)txn.opcode,
                            (unsigned)regs.mmu_fault_addr, (unsigned)txn.aux1,
                            (unsigned)m68k_areg(regs, 7));
                }
                return false;
            }
            if (canonicalize_target_fetch) {
                /* Confirmed user JSR target-fetch seam: the target instruction
                 * fetch faults after the return address push has architecturally
                 * completed.  Preserve that post-push stack and frame the bus
                 * error at the target PC; returning to the JSR opcode would
                 * replay the push.  Keep this out of early low-PC probes, which
                 * still require the older retry/rollback behaviour. */
                bridge_set_active_a7(txn.side_new);
                regs.fault_pc = txn.aux1;
                regs.instruction_pc = txn.aux1;
                m68k_setpc(txn.aux1);
                if (getenv("B2_JIT_TRACE_CALL_ROLLBACK")) {
                    fprintf(stderr,
                            "JIT_CALL_TARGET_CANONICALIZE_TXN fault_pc=%08x op_pc=%08x op=%04x addr=%08x sp=%08x oldsp=%08x newsp=%08x\n",
                            (unsigned)fault_pc, (unsigned)txn.pc, (unsigned)txn.opcode,
                            (unsigned)regs.mmu_fault_addr, (unsigned)m68k_areg(regs, 7),
                            (unsigned)txn.side_old, (unsigned)txn.side_new);
                }
                return true;
            }
            bridge_set_active_a7(txn.pre_a7);
            if (getenv("B2_JIT_TRACE_CALL_ROLLBACK")) {
                fprintf(stderr,
                        "JIT_CALL_TARGET_ROLLBACK_TXN fault_pc=%08x op_pc=%08x op=%04x addr=%08x sp=%08x oldsp=%08x newsp=%08x\n",
                        (unsigned)fault_pc, (unsigned)txn.pc, (unsigned)txn.opcode,
                        (unsigned)regs.mmu_fault_addr, (unsigned)txn.pre_a7,
                        (unsigned)txn.side_old, (unsigned)txn.side_new);
            }
            return true;
        }
        case bridge_mmu_txn_kind::return_pop:
            if (txn.pc != fault_pc || (txn.opcode != 0x4e75u && txn.opcode != 0x4e77u)) {
                if (getenv("B2_JIT_TRACE_CALL_ROLLBACK")) {
                    fprintf(stderr,
                            "JIT_RETURN_TARGET_ROLLBACK_TXN_MISS fault_pc=%08x op_pc=%08x op=%04x addr=%08x sp=%08x oldsp=%08x newsp=%08x\n",
                            (unsigned)fault_pc, (unsigned)txn.pc, (unsigned)txn.opcode,
                            (unsigned)regs.mmu_fault_addr, (unsigned)m68k_areg(regs, 7),
                            (unsigned)txn.side_old, (unsigned)txn.side_new);
                }
                return false;
            }
            if (m68k_areg(regs, 7) != txn.side_new && m68k_areg(regs, 7) != txn.pre_a7) {
                if (getenv("B2_JIT_TRACE_CALL_ROLLBACK")) {
                    fprintf(stderr,
                            "JIT_RETURN_TARGET_ROLLBACK_TXN_SP_MISS fault_pc=%08x op_pc=%08x op=%04x addr=%08x sp=%08x oldsp=%08x newsp=%08x\n",
                            (unsigned)fault_pc, (unsigned)txn.pc, (unsigned)txn.opcode,
                            (unsigned)regs.mmu_fault_addr, (unsigned)m68k_areg(regs, 7),
                            (unsigned)txn.side_old, (unsigned)txn.side_new);
                }
                return false;
            }
            bridge_set_active_a7(txn.pre_a7);
            if (getenv("B2_JIT_TRACE_CALL_ROLLBACK")) {
                fprintf(stderr,
                        "JIT_RETURN_TARGET_ROLLBACK_TXN fault_pc=%08x op_pc=%08x op=%04x addr=%08x sp=%08x oldsp=%08x newsp=%08x\n",
                        (unsigned)fault_pc, (unsigned)txn.pc, (unsigned)txn.opcode,
                        (unsigned)regs.mmu_fault_addr, (unsigned)txn.pre_a7,
                        (unsigned)txn.side_old, (unsigned)txn.side_new);
            }
            return true;
        case bridge_mmu_txn_kind::none:
        default:
            return false;
    }
}

static void bridge_restore_postinc_if_faulted(int areg, int inc, uae_u32 fault_addr)
{
    if (areg < 0 || areg > 7 || inc <= 0)
        return;
    uae_u32 &value = m68k_areg(regs, areg);
    if (value == fault_addr + (uae_u32)inc)
        value = fault_addr;
}

static void bridge_restore_predec_if_faulted(int areg, int inc, uae_u32 fault_addr)
{
    if (areg < 0 || areg > 7 || inc <= 0)
        return;
    uae_u32 &value = m68k_areg(regs, areg);
    if (value == fault_addr)
        value = fault_addr + (uae_u32)inc;
}

static bool bridge_is_bsr_opcode(uae_u16 opcode)
{
    return (opcode & 0xff00u) == 0x6100u;
}

static bool bridge_is_absolute_control_opcode(uae_u16 opcode)
{
    return opcode == 0x4eb9u || opcode == 0x4ef9u;
}

static bool bridge_is_stack_push_opcode(uae_u16 opcode)
{
    return (opcode & 0xff00u) == 0x2f00u;
}

static void bridge_restore_call_target_fault_side_effects(uae_u32 fault_pc)
{
    const uae_u32 fault_addr = regs.mmu_fault_addr;
    if (!fault_addr || fault_addr == fault_pc)
        return;
    /* A call-target rollback is only valid after the return-address push
     * succeeded and a later target code fetch faulted.  If the faulting address
     * is the just-decremented stack location, this is the call push itself
     * faulting; the 040 interpreter leaves that stack side effect visible for
     * exception delivery instead of undoing it here. */
    if (fault_addr + 4u == m68k_areg(regs, 7))
        return;
    if (bridge_rollback_mmu_txn(fault_pc))
        return;

    /* Native BSR lowers A7 and stores the return address before control reaches
     * the target fetch.  If that target fetch raises a 040 MMU fault, retrying
     * the BSR without rollback pushes the return address a second time.  The
     * low virtual post-root failure at 00003372 -> 00012b04 has exactly this
     * signature; the published fault_pc can be two bytes before the live BSR
     * word, so scan a tiny window around it rather than trusting only fop. */
    for (int delta = 0; delta <= 2; delta += 2) {
        const uae_u32 op_pc = fault_pc + (uae_u32)delta;
        if (!bridge_live_readable(op_pc, 2))
            continue;
        const uae_u16 opcode = (uae_u16)Uae2026JitLiveGetWord(op_pc);
        if (!bridge_is_bsr_opcode(opcode))
            continue;
        if (op_pc != fault_pc) {
            const uae_u16 fault_opcode = bridge_live_peek_word(fault_pc);
            if (bridge_is_absolute_control_opcode(fault_opcode) || bridge_is_stack_push_opcode(fault_opcode))
                continue;
        }
        const uae_u32 restored_sp = m68k_areg(regs, 7) + 4;
        bridge_set_active_a7(restored_sp);
        if (getenv("B2_JIT_TRACE_CALL_ROLLBACK")) {
            fprintf(stderr,
                    "JIT_CALL_TARGET_ROLLBACK fault_pc=%08x op_pc=%08x op=%04x addr=%08x sp=%08x\n",
                    (unsigned)fault_pc, (unsigned)op_pc, (unsigned)opcode,
                    (unsigned)fault_addr, (unsigned)restored_sp);
        }
        return;
    }
}

static void bridge_restore_autoea_fault_side_effects(uae_u32 fault_pc, bool restartable)
{
    if (!bridge_live_readable(fault_pc, 2))
        return;
    const uae_u16 opcode = (uae_u16)Uae2026JitLiveGetWord(fault_pc);
    const uae_u32 fault_addr = regs.mmu_fault_addr;

    /* MOVES.<size> <reg>,(An)+ / -(An) and memory->reg variants.  Native and
       mixed fallback paths can update An before helper memory access longjmps
       to the bridge.  Postincrement rollback is exact and safe whenever the
       register equals fault_addr+inc; predecrement rollback remains limited to
       restartable faults so a visible faulting predecrement write is not undone. */
    int moves_inc = 0;
    if ((opcode & 0xfff8u) == 0x0e18u || (opcode & 0xfff8u) == 0x0e20u)
        moves_inc = areg_byteinc[opcode & 7u];
    else if ((opcode & 0xfff8u) == 0x0e58u || (opcode & 0xfff8u) == 0x0e60u)
        moves_inc = 2;
    else if ((opcode & 0xfff8u) == 0x0e98u || (opcode & 0xfff8u) == 0x0ea0u)
        moves_inc = 4;
    if (moves_inc > 0) {
        const int reg = opcode & 7u;
        if ((opcode & 0x38u) == 0x18u)
            bridge_restore_postinc_if_faulted(reg, moves_inc, fault_addr);
        else if (restartable && (opcode & 0x38u) == 0x20u)
            bridge_restore_predec_if_faulted(reg, moves_inc, fault_addr);
        return;
    }

    /* Generic MOVE.<size> with auto-update source/destination modes.  This is
       intentionally conservative: restore postincrement only on the exact
       fault_addr+inc signature, and restore predecrement only for restartable
       faults. */
    if ((opcode & 0xc000u) == 0 && (opcode & 0x3000u) != 0) {
        const int src_mode = (opcode >> 3) & 7;
        const int src_reg = opcode & 7;
        const int dst_mode = (opcode >> 6) & 7;
        const int dst_reg = (opcode >> 9) & 7;
        const int src_inc = bridge_move_size_increment(opcode, src_reg);
        const int dst_inc = bridge_move_size_increment(opcode, dst_reg);
        if (src_mode == 3)
            bridge_restore_postinc_if_faulted(src_reg, src_inc, fault_addr);
        else if (restartable && src_mode == 4)
            bridge_restore_predec_if_faulted(src_reg, src_inc, fault_addr);
        if (dst_mode == 3)
            bridge_restore_postinc_if_faulted(dst_reg, dst_inc, fault_addr);
        else if (restartable && dst_mode == 4)
            bridge_restore_predec_if_faulted(dst_reg, dst_inc, fault_addr);
    }
}

static previous_uae2026_prefs snapshot_bridge_prefs()
{
    previous_uae2026_prefs prefs = {};
    prefs.requested = env_truthy("PREVIOUS_UAE2026_JIT", false);
    prefs.bootstrap_enabled = env_truthy("PREVIOUS_UAE2026_JIT_BOOTSTRAP", prefs.requested);
    prefs.aslr_active = env_truthy("PREVIOUS_ASLR_ACTIVE", false);
#if defined(__aarch64__)
    prefs.host_supported = true;
#else
    prefs.host_supported = false;
#endif
    prefs.runtime_disabled = true;
    prefs.cachesize_kb = env_int("PREVIOUS_UAE2026_JIT_CACHE_KB", 8192);
    prefs.jit_fpu = env_truthy("PREVIOUS_UAE2026_JIT_FPU", false);
    prefs.lazy_flush = env_truthy("PREVIOUS_UAE2026_JIT_LAZY_FLUSH", true);
    prefs.const_jump = env_truthy("PREVIOUS_UAE2026_JIT_CONST_JUMP", true);
    prefs.compiler_prefs_applied = compiler_prefs_applied;
    if (prefs.compiler_prefs_applied && Uae2026CompilerPrefsShimAvailable()) {
        prefs.cachesize_kb = Uae2026CompilerPrefsCacheSizeKB();
        prefs.jit_fpu = Uae2026CompilerPrefsFPUEnabled();
        prefs.const_jump = Uae2026CompilerPrefsConstJumpEnabled();
        prefs.lazy_flush = !Uae2026CompilerPrefsHardFlushEnabled();
    }
    prefs.cpu_model = currprefs.cpu_model;
    prefs.cpu_level = currprefs.cpu_level;
    prefs.mmu_model = currprefs.mmu_model;
    prefs.fpu_model = currprefs.fpu_model;
    prefs.fpu_revision = currprefs.fpu_revision;
    prefs.cpu_compatible = currprefs.cpu_compatible;
    prefs.fpu_strict = currprefs.fpu_strict;
    prefs.bootstrap_ready = prefs.requested && prefs.bootstrap_enabled && prefs.aslr_active &&
                            prefs.host_supported && prefs.cpu_model >= 68020 &&
                            prefs.mmu_model != 0 && prefs.cachesize_kb >= 1024 &&
                            (!Uae2026CompilerPrefsShimAvailable() || prefs.compiler_prefs_applied);
    return prefs;
}

static const char *bool_word(bool value)
{
    return value ? "yes" : "no";
}

static bool ensure_bootstrap_cache(const previous_uae2026_prefs &prefs)
{
    if (!prefs.bootstrap_ready)
        return false;
    if (bootstrap_cache)
        return true;

    bootstrap_attempted = true;
    bootstrap_cache_bytes = (size_t)prefs.cachesize_kb * 1024;

#if defined(__linux__) || defined(__APPLE__)
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        page_size = 4096;
    const size_t aligned_bytes = (bootstrap_cache_bytes + (size_t)page_size - 1) & ~((size_t)page_size - 1);
    bootstrap_cache_bytes = aligned_bytes;
    bootstrap_cache = mmap(nullptr,
                           bootstrap_cache_bytes,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1,
                           0);
    if (bootstrap_cache == MAP_FAILED) {
        bootstrap_cache = nullptr;
        bootstrap_cache_bytes = 0;
        return false;
    }
#else
    bootstrap_cache = malloc(bootstrap_cache_bytes);
    if (!bootstrap_cache) {
        bootstrap_cache_bytes = 0;
        return false;
    }
#endif

    memset(bootstrap_cache, 0, bootstrap_cache_bytes);
#if defined(__GNUC__)
    __builtin___clear_cache((char *)bootstrap_cache, (char *)bootstrap_cache + bootstrap_cache_bytes);
#endif
    bootstrap_active = true;
    return true;
}

static const char *update_bridge_summary()
{
    const previous_uae2026_prefs prefs = snapshot_bridge_prefs();
    const bool ready = prefs.bootstrap_ready;
    const bool attempted = bootstrap_attempted;
    const bool active = bootstrap_cache != nullptr && bootstrap_active;
    const unsigned long long cache_addr = (unsigned long long)(uintptr_t)bootstrap_cache;

    snprintf(bridge_summary, sizeof(bridge_summary),
             "uae2026 bridge compiled; requested=%s; bootstrap_enabled=%s; bootstrap_ready=%s; bootstrap_attempted=%s; bootstrap_active=%s; compiler_prefs=%s; compiler_init=%s; jit_active=%s; aslr=%s; host=%s; cpu=%d/mmu=%d/fpu=%d($%02x); cache=%dKB; jit_fpu=%s; lazy_flush=%s; const_jump=%s; runtime_jit=%s; cache_addr=0x%llx; cache_bytes=%zu",
             bool_word(prefs.requested), bool_word(prefs.bootstrap_enabled), bool_word(ready),
             bool_word(attempted), bool_word(active), bool_word(prefs.compiler_prefs_applied),
             bool_word(compiler_initialized), bool_word(jit_active), bool_word(prefs.aslr_active), host_arch(),
             prefs.cpu_model, prefs.mmu_model, prefs.fpu_model, prefs.fpu_revision,
             prefs.cachesize_kb, bool_word(prefs.jit_fpu), bool_word(prefs.lazy_flush),
             bool_word(prefs.const_jump), jit_active ? "enabled" : "disabled", cache_addr, bootstrap_cache_bytes);
    return bridge_summary;
}
} // namespace

extern "C" bool Uae2026JitBridgeCompiled(void)
{
    return true;
}

extern "C" void Uae2026JitMmuTxnClear(void)
{
    bridge_clear_mmu_txn();
}

extern "C" void Uae2026JitMmuTxnBeginCallPush(uae_u32 pc, uae_u32 opcode, uae_u32 pre_a7, uae_u32 pushed_a7, uae_u32 return_pc)
{
    bridge_active_mmu_txn.kind = bridge_mmu_txn_kind::call_push;
    bridge_active_mmu_txn.pc = pc;
    bridge_active_mmu_txn.opcode = (uae_u16)opcode;
    bridge_active_mmu_txn.pre_sr = regs.sr;
    bridge_active_mmu_txn.pre_a7 = pre_a7;
    bridge_active_mmu_txn.side_old = pre_a7;
    bridge_active_mmu_txn.side_new = pushed_a7;
    bridge_active_mmu_txn.aux0 = return_pc;
    bridge_active_mmu_txn.aux1 = 0;
}

extern "C" void Uae2026JitMmuTxnBeginCallPushPreTarget(uae_u32 pc, uae_u32 opcode, uae_u32 pre_a7, uae_u32 target_pc)
{
    bridge_active_mmu_txn.kind = bridge_mmu_txn_kind::call_push;
    bridge_active_mmu_txn.pc = pc;
    bridge_active_mmu_txn.opcode = (uae_u16)opcode;
    bridge_active_mmu_txn.pre_sr = regs.sr;
    bridge_active_mmu_txn.pre_a7 = pre_a7;
    bridge_active_mmu_txn.side_old = pre_a7;
    bridge_active_mmu_txn.side_new = pre_a7 - 4;
    bridge_active_mmu_txn.aux0 = 0;
    bridge_active_mmu_txn.aux1 = target_pc;
}

extern "C" void Uae2026JitMmuTxnBeginCallPushPreTargetCurrentA7(uae_u32 pc, uae_u32 target_pc)
{
    if (!regs.mmu_enabled)
        return;
    const uae_u32 opcode = bridge_live_readable(pc, 2) ? Uae2026JitLiveGetWord(pc) : 0;
    Uae2026JitMmuTxnBeginCallPushPreTarget(pc, opcode, m68k_areg(regs, 7), target_pc);
}

static uae_u16 bridge_host_word_at_current_pc(uae_u32 pc, uae_u32 word_offset)
{
    if (!regs.pc_p || m68k_getpc() != pc)
        return 0;
    const uae_u8 *p = regs.pc_p + word_offset;
    return ((uae_u16)p[0] << 8) | p[1];
}

extern "C" void Uae2026JitMmuTxnBeginCallPushCurrentA7ForOpcode(uae_u32 pc, uae_u32 opcode)
{
    if (!regs.mmu_enabled)
        return;

    uae_u32 target_pc = 0;
    const uae_u16 op = (uae_u16)opcode;
    if (bridge_is_bsr_opcode(op)) {
        uae_s32 disp = 0;
        const uae_u8 low = op & 0xffu;
        if (low == 0x00u)
            disp = (uae_s32)(uae_s16)bridge_host_word_at_current_pc(pc, 2);
        else if (low == 0xffu)
            disp = (uae_s32)(((uae_u32)bridge_host_word_at_current_pc(pc, 2) << 16) |
                             (uae_u32)bridge_host_word_at_current_pc(pc, 4));
        else
            disp = (uae_s32)(uae_s8)low;
        target_pc = pc + 2u + (uae_u32)disp;
    } else if ((op & 0xffc0u) == 0x4e80u) { /* JSR */
        const int mode = (op >> 3) & 7;
        const int reg = op & 7;
        switch (mode) {
            case 2: /* (An) */
                target_pc = m68k_areg(regs, reg);
                break;
            case 5: /* (d16,An) */
                target_pc = m68k_areg(regs, reg) + (uae_u32)(uae_s32)(uae_s16)bridge_host_word_at_current_pc(pc, 2);
                break;
            case 7:
                if (reg == 0) { /* (xxx).W */
                    target_pc = (uae_u32)(uae_s32)(uae_s16)bridge_host_word_at_current_pc(pc, 2);
                } else if (reg == 1) { /* (xxx).L */
                    target_pc = ((uae_u32)bridge_host_word_at_current_pc(pc, 2) << 16) |
                                (uae_u32)bridge_host_word_at_current_pc(pc, 4);
                } else if (reg == 2) { /* (d16,PC) */
                    target_pc = pc + 2u + (uae_u32)(uae_s32)(uae_s16)bridge_host_word_at_current_pc(pc, 2);
                }
                break;
            default:
                break;
        }
    }

    if (target_pc)
        Uae2026JitMmuTxnBeginCallPushPreTarget(pc, op, m68k_areg(regs, 7), target_pc);
}

extern "C" void Uae2026JitMmuTxnBeginCallPushTarget(uae_u32 pc, uae_u32 target_pc)
{
    const uae_u32 pushed_a7 = m68k_areg(regs, 7);
    const uae_u32 opcode = bridge_live_readable(pc, 2) ? Uae2026JitLiveGetWord(pc) : 0;
    bridge_active_mmu_txn.kind = bridge_mmu_txn_kind::call_push;
    bridge_active_mmu_txn.pc = pc;
    bridge_active_mmu_txn.opcode = (uae_u16)opcode;
    bridge_active_mmu_txn.pre_sr = regs.sr;
    bridge_active_mmu_txn.pre_a7 = pushed_a7 + 4;
    bridge_active_mmu_txn.side_old = pushed_a7 + 4;
    bridge_active_mmu_txn.side_new = pushed_a7;
    bridge_active_mmu_txn.aux0 = 0;
    bridge_active_mmu_txn.aux1 = target_pc;
}

extern "C" void Uae2026JitMmuTxnBeginReturnPop(uae_u32 pc, uae_u32 opcode, uae_u32 pre_a7, uae_u32 pop_bytes)
{
    bridge_active_mmu_txn.kind = bridge_mmu_txn_kind::return_pop;
    bridge_active_mmu_txn.pc = pc;
    bridge_active_mmu_txn.opcode = (uae_u16)opcode;
    bridge_active_mmu_txn.pre_sr = regs.sr;
    bridge_active_mmu_txn.pre_a7 = pre_a7;
    bridge_active_mmu_txn.side_old = pre_a7;
    bridge_active_mmu_txn.side_new = pre_a7 + pop_bytes;
    bridge_active_mmu_txn.aux0 = 0;
    bridge_active_mmu_txn.aux1 = 0;
}

extern "C" void Uae2026JitMmuTxnBeginReturnPopCurrentA7(uae_u32 pc, uae_u32 opcode, uae_u32 pop_bytes)
{
    if (!regs.mmu_enabled)
        return;
    Uae2026JitMmuTxnBeginReturnPop(pc, opcode, m68k_areg(regs, 7), pop_bytes);
}

extern "C" void Uae2026JitMmuTxnBeginReturnPopCurrentA7ByOpcode(uae_u32 pc, uae_u32 opcode)
{
    uae_u32 pop_bytes = 0;
    if ((uae_u16)opcode == 0x4e75u)
        pop_bytes = 4;
    else if ((uae_u16)opcode == 0x4e77u)
        pop_bytes = 6;
    if (pop_bytes)
        Uae2026JitMmuTxnBeginReturnPopCurrentA7(pc, opcode, pop_bytes);
}

extern "C" void Uae2026JitMmuTxnCommit(void)
{
    bridge_clear_mmu_txn();
}

extern "C" bool Uae2026JitBridgeRequested(void)
{
    return snapshot_bridge_prefs().requested;
}

extern "C" bool Uae2026JitBridgeBootstrapReady(void)
{
    return snapshot_bridge_prefs().bootstrap_ready;
}

extern "C" bool Uae2026JitBridgeBootstrapAttempted(void)
{
    return bootstrap_attempted;
}

extern "C" bool Uae2026JitBridgeBootstrapActive(void)
{
    return bootstrap_cache != nullptr && bootstrap_active;
}

extern "C" const char *Uae2026JitBridgeSummary(void)
{
    return update_bridge_summary();
}

extern "C" bool Uae2026JitBridgeIsActive(void)
{
    return jit_active && UseJIT;
}

extern "C" void Uae2026JitBridgeRequestBlockExit(unsigned int source)
{
    if (!jit_active || !env_truthy("PREVIOUS_UAE2026_JIT_RAM", false))
        return;
    regs.spcflags |= 0x800; /* SPCFLAG_MODE_CHANGE: leave compiled block safely */
    block_exit_request_count++;
    if (block_exit_request_count <= 16 || (block_exit_request_count % 1024) == 0)
        fprintf(stderr, "UAE2026 bridge: requested JIT block exit source=%08x count=%lu\n",
                source, block_exit_request_count);
}

extern "C" void Uae2026JitBridgeCompileExecute(void)
{
    if (!jit_active)
        return;

    /* Since regs is now the shared symbol with JIT fields at correct
     * offsets (via newcpu.h restructure), no register sync is needed.
     * Sync the flag struct between Previous's cznv layout and JIT's nzcv. */
    jit_regflags.nzcv = regflags.cznv;
    jit_regflags.x    = regflags.x;

    /* Update shadow ROM/VRAM/RAM before JIT dispatch so direct reads see current state. */
    Uae2026JitBridgeSyncOpcodeTestShadow();
    sync_shadow_video();
    {
        static int full_ram_sync = -1;
        if (full_ram_sync < 0) {
            const char *env = getenv("PREVIOUS_JIT_FULL_RAM_TO_SHADOW");
            full_ram_sync = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        }
        if (full_ram_sync && jit_MEMBaseDiff) {
            extern uae_u8 NEXTRam[];
            const size_t ram_size = 64UL * 1024 * 1024;
            memcpy((void *)(jit_MEMBaseDiff + 0x04000000), NEXTRam, ram_size);
        }
    }

    /* Ensure pc_p is NULL so execute_normal() always re-derives from
     * regs.pc (which m68k_reset sets correctly) on every dispatch.     */
    regs.pc_p    = nullptr;
    regs.pc_oldp = nullptr;

    bool handled_mmu_exception = false;
    int prb = setjmp(__exbuf);
    if (prb == 0) {
        __exvalue = 0;
        __pushtry(&__exbuf);
        m68k_do_compile_execute();
        __poptry();
    } else {
        handled_mmu_exception = true;
        __exvalue = prb;
        if (__is_catched())
            __poptry();
        if (mmu_restart) {
            regflags = Uae2026JitLastFlags;
            const uae_u32 restart_pc = regs.fault_pc ? regs.fault_pc :
                (Uae2026JitLastInstructionPc ? Uae2026JitLastInstructionPc : regs.instruction_pc);
            m68k_setpc(restart_pc);
        }
        for (unsigned fixup_index = 0; fixup_index < 2; fixup_index++) {
            if (mmufixup[fixup_index].reg >= 0) {
                m68k_areg(regs, mmufixup[fixup_index].reg) = mmufixup[fixup_index].value;
                mmufixup[fixup_index].reg = -1;
            }
        }
        bridge_restore_autoea_fault_side_effects(regs.fault_pc, mmu_restart);
        bridge_restore_call_target_fault_side_effects(regs.fault_pc);
        /* Confirmed user write seams: the 040 interpreter reports these
         * non-restartable byte-store faults after advancing PC to the next
         * instruction.  Native helper faults arrive with PC still on the write,
         * causing replay while source/destination side effects have already
         * completed (for example MOVE.B (A2)+,(A0) at 0500bc98). */
        if (prb == 2 && !mmu_restart &&
            (regs.fault_pc == 0x0500b6aeu || regs.fault_pc == 0x0500bc98u)) {
            regs.fault_pc += 2;
            regs.instruction_pc = regs.fault_pc;
            m68k_setpc(regs.fault_pc);
        }
        const bool bridge_rte_fault = bridge_live_peek_word(regs.fault_pc) == 0x4e73u;
        /* If a RAM/MMU fault escapes while RTE has only partially completed,
         * the generated 040 handler has already loaded the frame SR and may
         * have switched A7 to USP.  Exception(2) must describe the faulting RTE
         * instruction, not stack a synthetic user-mode fault against the handler
         * itself; restore the cached pre-instruction supervisor state first. */
        if (bridge_rte_fault && !regs.s && (Uae2026JitLastSr & 0x2000u)) {
            regs.sr = (uae_u16)Uae2026JitLastSr;
            MakeFromSR();
            m68k_areg(regs, 7) = Uae2026JitLastA7;
            if (regs.m)
                regs.msp = Uae2026JitLastA7;
            else
                regs.isp = Uae2026JitLastA7;
        }
        /* If RTE already switched to user mode before faulting, MakeFromSR()
         * should have saved the post-pop supervisor stack in regs.isp.  Do not
         * overwrite that with the pre-RTE exception-frame SP; use the cached
         * value only as a last-ditch fallback for missing ISP state. */
        if (!regs.s && Uae2026JitLastExceptionSp && bridge_rte_fault && regs.isp == 0) {
            regs.isp = Uae2026JitLastExceptionSp;
        }
        /* Native/fallback RAM MMU helpers can longjmp after the generated
         * instruction has advanced regs.pc, but a 68040 access-error frame
         * must be built from the faulting instruction PC.  Keep this narrow to
         * kernel RAM text; low-virtual call faults can publish extension-word
         * PCs and are handled by explicit call/return transactions above. */
        if (prb == 2 && !bridge_rte_fault && regs.fault_pc < 0x00020000u &&
            regs.mmu_fault_addr != regs.fault_pc) {
            const uae_u16 fc = regs.mmu_ssw & 0x0007u; /* 68040 SSW TM/function-code bits */
            if (fc != 2 && fc != 6)
                regs.mmu_effective_addr = regs.mmu_fault_addr;
        }
        if (prb == 2 && !bridge_rte_fault && regs.fault_pc >= 0x04000000u && regs.fault_pc < 0x08000000u) {
            /* Previous's legacy format-7 frame builder stores mmu_effective_addr
             * as the EA word.  JIT-delivered helper faults may leave that field
             * stale from an earlier low-virtual fault; make it match the actual
             * bus fault address for bridge-delivered RAM/MMU data cycles. */
            regs.mmu_effective_addr = regs.mmu_fault_addr;
            if (m68k_getpc() != regs.fault_pc)
                m68k_setpc(regs.fault_pc);
        }
        {
            static unsigned long exc_log_count = 0;
            const uae_u32 pc = m68k_getpc();
            if (exc_log_count < 256 || pc == 0x0406b94au || pc == 0x040a4b52u || (exc_log_count % 1024) == 0) {
                const uae_u32 sp = regs.regs[15];
                const uae_u32 fault_pc = regs.fault_pc;
                fprintf(stderr,
                        "UAE2026 bridge: caught MMU exception %d pc=%08x op=%04x ext=%04x fault_pc=%08x fop=%04x fext=%04x addr=%08x sr=%04x sfc=%u dfc=%u vbr=%08x vec2=%08x vec3=%08x sp=%08x "
                        "d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x "
                        "a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x a7=%08x "
                        "sp0=%08x sp4=%08x sp8=%08x fr_sr=%04x fr_pc=%08x fr_vec=%04x fr8=%08x fr12=%08x spc=%08x\n",
                        prb, (unsigned)pc,
                        bridge_live_readable(pc, 4) ? (unsigned)Uae2026JitLiveGetWord(pc) : 0xffffu,
                        bridge_live_readable(pc + 2, 2) ? (unsigned)Uae2026JitLiveGetWord(pc + 2) : 0xffffu,
                        (unsigned)fault_pc,
                        bridge_live_readable(fault_pc, 2) ? (unsigned)Uae2026JitLiveGetWord(fault_pc) : 0xffffu,
                        bridge_live_readable(fault_pc + 2, 2) ? (unsigned)Uae2026JitLiveGetWord(fault_pc + 2) : 0xffffu,
                        (unsigned)regs.mmu_fault_addr,
                        (unsigned)regs.sr, (unsigned)regs.sfc, (unsigned)regs.dfc,
                        (unsigned)regs.vbr,
                        bridge_live_peek_long(regs.vbr + 8),
                        bridge_live_peek_long(regs.vbr + 12),
                        (unsigned)sp,
                        (unsigned)regs.regs[0], (unsigned)regs.regs[1],
                        (unsigned)regs.regs[2], (unsigned)regs.regs[3],
                        (unsigned)regs.regs[4], (unsigned)regs.regs[5],
                        (unsigned)regs.regs[6], (unsigned)regs.regs[7],
                        (unsigned)regs.regs[8], (unsigned)regs.regs[9],
                        (unsigned)regs.regs[10], (unsigned)regs.regs[11],
                        (unsigned)regs.regs[12], (unsigned)regs.regs[13],
                        (unsigned)regs.regs[14], (unsigned)regs.regs[15],
                        bridge_live_peek_long(sp), bridge_live_peek_long(sp + 4),
                        bridge_live_peek_long(sp + 8),
                        bridge_live_peek_word(sp), bridge_live_peek_long(sp + 2),
                        bridge_live_peek_word(sp + 6), bridge_live_peek_long(sp + 8),
                        bridge_live_peek_long(sp + 12), (unsigned)regs.spcflags);
            }
            exc_log_count++;
            if ((env_truthy("B2_JIT_TRACE_LOW33_PCP", false) || env_truthy("B2_JIT_TRACE_LOWPC_PCP", false)) &&
                prb == 2 && regs.fault_pc < 0x00020000u &&
                (env_truthy("B2_JIT_TRACE_LOWPC_PCP", false) ||
                 (regs.fault_pc >= 0x00003300u && regs.fault_pc <= 0x00003400u))) {
                const uae_u8 *pcp = regs.pc_p;
                fprintf(stderr,
                        "JIT_LOWPC_PCP pc=%08x fault_pc=%08x addr=%08x pc_p=%p oldp=%p pcpw0=%04x pcpw1=%04x pcpw2=%04x pcpw3=%04x dataw0=%04x dataw1=%04x dataw2=%04x dataw3=%04x\n",
                        (unsigned)m68k_getpc(), (unsigned)regs.fault_pc,
                        (unsigned)regs.mmu_fault_addr, (const void *)regs.pc_p,
                        (const void *)regs.pc_oldp,
                        (unsigned)bridge_shadow_host_peek_word(pcp),
                        (unsigned)bridge_shadow_host_peek_word(pcp + 2),
                        (unsigned)bridge_shadow_host_peek_word(pcp + 4),
                        (unsigned)bridge_shadow_host_peek_word(pcp + 6),
                        (unsigned)bridge_live_peek_word(regs.fault_pc),
                        (unsigned)bridge_live_peek_word(regs.fault_pc + 2),
                        (unsigned)bridge_live_peek_word(regs.fault_pc + 4),
                        (unsigned)bridge_live_peek_word(regs.fault_pc + 6));
            }
        }
        bridge_trace_lowpc_resume("PRE", prb);
        int prb2 = setjmp(__exbuf);
        if (prb2 == 0) {
            __exvalue = 0;
            __pushtry(&__exbuf);
            prev_Exception_1arg(prb);
            __poptry();
            if (regs.s) {
                Uae2026JitLastExceptionSp = m68k_areg(regs, 7);
                if (regs.m)
                    regs.msp = m68k_areg(regs, 7);
                else
                    regs.isp = m68k_areg(regs, 7);
            }
            MakeSR();
            uae_u32 handled_pc = m68k_getpc();
            if (prb == 2 && handled_pc == 0) {
                const uae_u32 vec2 = bridge_live_peek_long(regs.vbr + 8);
                if (vec2) {
                    handled_pc = vec2;
                    regs.s = 1;
                    regs.m = 0;
                    MakeSR();
                }
            }
            /* Exception() can leave the correct post-vector PC represented by
             * the pc/pc_p/pc_oldp triple without committing it to regs.pc.  The
             * bridge deliberately clears pc_p at the next JIT entry, so make the
             * vector PC canonical now or the next dispatch can see PC=0. */
            m68k_setpc(handled_pc);
            bridge_trace_lowpc_resume("POST", prb);
            if (prb == 2) {
                static unsigned long handled_log_count = 0;
                if (handled_log_count < 64 || (handled_log_count % 1024) == 0 ||
                    (regs.fault_pc == 0x04001660u && env_truthy("B2_JIT_TRACE_MMU_FRAME", false))) {
                    fprintf(stderr,
                            "UAE2026 bridge: handled MMU exception %d newpc=%08x sr=%04x vbr=%08x sp=%08x sp0=%08x sp4=%08x fr_sr=%04x fr_pc=%08x fr_vec=%04x fr8=%08x fr12=%08x spc=%08x\n",
                            prb, (unsigned)handled_pc, (unsigned)regs.sr,
                            (unsigned)regs.vbr, (unsigned)regs.regs[15],
                            bridge_live_peek_long(regs.regs[15]),
                            bridge_live_peek_long(regs.regs[15] + 4),
                            bridge_live_peek_word(regs.regs[15]),
                            bridge_live_peek_long(regs.regs[15] + 2),
                            bridge_live_peek_word(regs.regs[15] + 6),
                            bridge_live_peek_long(regs.regs[15] + 8),
                            bridge_live_peek_long(regs.regs[15] + 12),
                            (unsigned)regs.spcflags);
                }
                handled_log_count++;
            }
            const bool explicit_rte_handoff = env_truthy("B2_JIT_RTE_FAULT_HANDOFF", false) &&
                !env_truthy("B2_JIT_RTE_FAULT_HANDOFF_DISABLE", false);
            if (bridge_rte_fault && explicit_rte_handoff) {
                fprintf(stderr, "UAE2026 bridge: RTE fault handoff to interpreter pc=%08x sr=%04x isp=%08x\n",
                        (unsigned)handled_pc, (unsigned)regs.sr, (unsigned)regs.isp);
                UseJIT = false;
                jit_active = false;
            }
        } else {
            __exvalue = prb2;
            if (__is_catched())
                __poptry();
            fprintf(stderr, "UAE2026 bridge: fatal double MMU exception %d while handling %d\n", prb2, prb);
            prev_m68k_reset(1);
        }
    }

    /* Sync flag struct back.  If the bridge handled an MMU exception,
     * Exception() and the interpreter-side path have already updated
     * regflags; do not overwrite them with stale JIT-entry flags. */
    if (handled_mmu_exception) {
        jit_regflags.nzcv = regflags.cznv;
        jit_regflags.x    = regflags.x;
    } else {
        regflags.cznv = jit_regflags.nzcv;
        regflags.x    = jit_regflags.x;
    }

    /* Do not copy shadow RAM back over NEXTRam.  Device/DMA/interpreter
     * state is authoritative in Previous memory; the shadow is only for
     * JIT instruction fetch/direct-address reads. */
}

extern "C" void Uae2026JitBridgeInit(void)
{
    if (bridge_logged)
        return;
    bridge_logged = true;

    if (Uae2026CompilerPrefsShimAvailable()) {
        Uae2026CompilerPrefsSync(false);
        compiler_prefs_applied = true;
    }

    const previous_uae2026_prefs prefs = snapshot_bridge_prefs();
    if (!prefs.requested) {
        UseJIT = false;
        jit_active = false;
        fprintf(stderr,
                "UAE2026 bridge: %s; regstruct=%zu bytes; cpu_compatible=%s; fpu_strict=%s; special_mem_default=%d\n",
                update_bridge_summary(), sizeof(regstruct),
                bool_word(prefs.cpu_compatible), bool_word(prefs.fpu_strict),
                Uae2026CompilerPrefsSpecialMemDefault());
        return;
    }
    if (prefs.bootstrap_enabled)
        ensure_bootstrap_cache(prefs);

    /*
     * Call the real vendored compiler_init().
     * This sets up flush_icache, the baseaddr[]/mem_banks[] bank table,
     * and (when cache is ready) calls jit_force_translate_check().
     * Translated dispatch remains disabled — no block execution occurs.
     */
    compiler_init();
    compiler_initialized = true;

    /*
     * Full JIT bring-up:
     *  0. Uae2026CompilerPrefsSync() has already populated the vendored
     *                        JIT prefs (kept separate from Previous's native
     *                        currprefs because the struct layouts differ).
     *  1. init_table68k()  — build the 68k opcode table (jit_table68k[]).
     *                        BridgeInit() is called BEFORE init_m68k() in
     *                        hatari-glue.c so we must do this ourselves.
     *  2. build_comp()     — builds opcode property tables, allocates the
     *                        JIT code cache (create_popalls + alloc_cache),
     *                        writes the popall stubs (compemu_reset), and
     *                        sets cache_enabled = 1. All-in-one.
     */

    extern void init_table68k(void);
    init_table68k();
    build_comp();

    /* build_comp() calls alloc_cache() + compemu_reset() internally.
     * pushall_call_handler is set by create_popalls(); if it's NULL,
     * the JIT failed to initialise.                                    */
    extern void *pushall_call_handler;
    if (!pushall_call_handler) {
        fprintf(stderr, "UAE2026 bridge: build_comp failed to enable JIT cache\n");
        UseJIT = false;
        jit_active = false;
        if (compiler_initialized) {
            compiler_exit();
            compiler_initialized = false;
        }
        goto bri_init_done;
    }

    /* Point the JIT's bank-dispatch table at a layout-compatible table.
     * In RAM/MMU mode native data helpers must go through the 040 MMU path;
     * Previous's physical addrbank table bypasses address translation. */
    regs.mem_banks = env_truthy("PREVIOUS_UAE2026_JIT_RAM", false)
        ? Uae2026JitRamMmuBankTable()
        : (uintptr_t)mem_banks;

    /*
     * JIT Shadow Memory
     * -----------------
     * The JIT translates M68K addresses using  host = jit_MEMBaseDiff + mac_addr.
     * Previous uses non-contiguous host buffers (NEXTRom, NEXTRam) so a
     * single offset doesn't work.  We create a sparse shadow mapping that
     * covers the regions the JIT will actually access:
     *
     *   0x00000000..0x0001FFFF  ROM (at boot / address 0 overlay)
     *   0x01000000..0x0101FFFF  ROM (diagnostic mirror)
     *   0x04000000..0x07FFFFFF  RAM bank 0 (up to 64 MB)
     *
     * We mmap NEXT_RAM_START+128MB at a fixed high address (MAP_ANONYMOUS),
     * copy the ROM into positions 0x00000000 and 0x01000000 within it, then
     * set jit_MEMBaseDiff = shadow_base so host = shadow_base + mac_addr is valid
     * for all accessed regions.
     *
     * Also set RAMBaseHost so execute_normal()'s pc_p sanity guard fires and
     * re-derives pc_p from regs.pc (which m68k_reset() set correctly) using
     * get_real_address() = jit_MEMBaseDiff + mac_addr.
     */
    {
        extern uae_u8 NEXTRam[];
        extern uae_u8 NEXTRom[];
        extern uae_u8 *RAMBaseHost;
        extern uae_u32 RAMSize;
        extern uae_u8 *ROMBaseHost;
        extern uae_u32 ROMSize;
        extern uae_u32 ROMBaseMac;

        const uintptr_t shadow_size = 0x10040000UL; /* covers ROM, RAM, and 0x0b-0x0f VRAM windows */
        bool shadow_ready = false;

        uae_u8 *shadow = (uae_u8 *)mmap(
            NULL, shadow_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (shadow == MAP_FAILED) {
            fprintf(stderr, "UAE2026 bridge: shadow mmap failed (%s)\n", strerror(errno));
        } else {
            jit_shadow_base = shadow;
            jit_shadow_size = shadow_size;
            /* ROM at 0x00000000 and 0x01000000 */
            memcpy(shadow + 0x00000000, NEXTRom, 0x20000);
            memcpy(shadow + 0x01000000, NEXTRom, 0x20000);
            /* RAM at 0x04000000: mirror NEXTRam into the shadow region. */
            const size_t ram_size = 64UL * 1024 * 1024;
            memcpy(shadow + 0x04000000, NEXTRam, ram_size);
            /* Early ROM code sometimes uses monochrome VRAM as stack/scratch
             * around 0x0b03xxxx. Mirror VRAM and its monochrome MWF aliases so
             * any remaining direct-address reads do not fault. */
            sync_shadow_video();

            /* Make the shadow region executable so popall-derived JIT blocks
             * that land in the shadow don't fault.  Do not publish
             * jit_MEMBaseDiff until the mapping is fully usable; otherwise a
             * later failure path can leave helpers pointing at a half-initialized
             * shadow. */
            if (mprotect(shadow, shadow_size, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
                fprintf(stderr, "UAE2026 bridge: mprotect(shadow) failed (%s)\n", strerror(errno));
            } else {
                jit_MEMBaseDiff = (uintptr_t)shadow;
                /* Clear pc_p/pc_oldp so execute_normal() re-derives from regs.pc. *
                 * Also set the compiler unit's RAM/ROM host windows for guards.   */
                regs.pc_p = nullptr;
                regs.pc_oldp = nullptr;
                RAMBaseHost = shadow + 0x04000000;
                RAMSize = 64 * 1024 * 1024;
                ROMBaseHost = shadow + 0x01000000;
                ROMSize = 0x20000;
                ROMBaseMac = 0x01000000;
                shadow_ready = true;
            }
        }

        if (!shadow_ready) {
            UseJIT = false;
            jit_active = false;
            if (compiler_initialized) {
                compiler_exit();
                compiler_initialized = false;
            }
            if (jit_shadow_base) {
#if defined(__linux__) || defined(__APPLE__)
                munmap(jit_shadow_base, jit_shadow_size);
#else
                free(jit_shadow_base);
#endif
            }
            jit_shadow_base = nullptr;
            jit_shadow_size = 0;
            jit_MEMBaseDiff = 0;
            RAMBaseHost = nullptr;
            RAMSize = 0;
            ROMBaseHost = nullptr;
            ROMSize = 0;
            ROMBaseMac = 0;
            goto bri_init_done;
        }
    }

    /* Activate JIT dispatch */
    UseJIT = true;
    jit_active = true;

bri_init_done:
    fprintf(stderr,
            "UAE2026 bridge: %s; regstruct=%zu bytes; cpu_compatible=%s; fpu_strict=%s; special_mem_default=%d\n",
            update_bridge_summary(), sizeof(regstruct),
            bool_word(prefs.cpu_compatible), bool_word(prefs.fpu_strict),
            Uae2026CompilerPrefsSpecialMemDefault());
}

extern "C" void Uae2026JitBridgeSyncOpcodeTestShadow(void)
{
    extern uae_u8 NEXTRom[];
    if (!jit_shadow_base || jit_shadow_size < 0x01020000UL)
        return;
    /* Opcode-test setup patches NEXTRom after bridge init. Keep the JIT's
     * direct-addressing shadow in sync or compiled execution fetches stale
     * zeroes and runs off by MAXRUN bytes per dispatch. */
    memcpy(jit_shadow_base + 0x00000000, NEXTRom, 0x20000);
    memcpy(jit_shadow_base + 0x01000000, NEXTRom, 0x20000);
}

extern "C" void Uae2026JitBridgeShutdown(void)
{
    extern uae_u8 *RAMBaseHost;
    extern uae_u32 RAMSize;
    extern uae_u8 *ROMBaseHost;
    extern uae_u32 ROMSize;
    extern uae_u32 ROMBaseMac;

    if (jit_active) {
        UseJIT = false;
        jit_active = false;
    }
    if (compiler_initialized) {
        compiler_exit();
        compiler_initialized = false;
    }

    if (jit_shadow_base) {
#if defined(__linux__) || defined(__APPLE__)
        munmap(jit_shadow_base, jit_shadow_size);
#endif
        jit_shadow_base = nullptr;
        jit_shadow_size = 0;
    }
    jit_MEMBaseDiff = 0;
    RAMBaseHost = nullptr;
    RAMSize = 0;
    ROMBaseHost = nullptr;
    ROMSize = 0;
    ROMBaseMac = 0;

    if (bootstrap_cache) {
#if defined(__linux__) || defined(__APPLE__)
        munmap(bootstrap_cache, bootstrap_cache_bytes);
#else
        free(bootstrap_cache);
#endif
        bootstrap_cache = nullptr;
        bootstrap_cache_bytes = 0;
    }
    bootstrap_active = false;
}

#endif
