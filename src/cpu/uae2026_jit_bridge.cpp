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
#include "uae2026_opcode_test.h"
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
extern void jit_abort(const char *fmt, ...);
extern "C" void Uae2026CompilerFlushCacheHard(void);
extern "C" void Uae2026CompilerFlushCacheLazy(void);
extern "C" void Uae2026JitDiagnosticReport(void);

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
extern "C" bool mmu_probe_atc(uaecptr addr, bool super, uaecptr *out_phys);
extern uae_u16 mmu_opcode;

extern "C" void Uae2026JitPublishTraceInstructionState(uae_u32 pc, uae_u16 opcode)
{
    regs.instruction_pc = pc;
    regs.mmu_effective_addr = 0;
    mmu_opcode = opcode;
}

/* UseJIT flag (defined in uae2026_linker_stubs.cpp) */
extern bool UseJIT;

/* MEMBaseDiff -- host base offset for the JIT's direct-addressing  */
extern uintptr_t jit_MEMBaseDiff;
extern "C" uintptr_t Uae2026JitRamMmuBankTable(void);
extern "C" void Uae2026JitSyncRamToShadow(void);
extern "C" uae_u32 Uae2026JitMmuXlateData(uae_u32 addr);
extern "C" uintptr_t Uae2026JitMmuXlateCodeHost(uae_u32 addr);
extern "C" uae_u32 Uae2026JitLiveGetByte(uae_u32 addr);
extern "C" uae_u32 Uae2026JitLiveGetWord(uae_u32 addr);
extern "C" uae_u32 Uae2026JitLiveGetLong(uae_u32 addr);
extern "C" void Uae2026JitLivePutByte(uae_u32 addr, uae_u32 value);
extern "C" void Uae2026JitLivePutWord(uae_u32 addr, uae_u32 value);
extern "C" void Uae2026JitLivePutLong(uae_u32 addr, uae_u32 value);
extern "C" uae_u32 Uae2026JitLastInstructionPc;
extern "C" uae_u32 Uae2026JitLastSr;
extern "C" uae_u32 Uae2026JitLastA7;
extern "C" uae_u32 Uae2026JitLowpcFaultSeq;
extern "C" uae_u32 Uae2026JitLastLowpcFaultPc;
extern "C" uae_u32 Uae2026JitLastLowpcFaultAddr;
extern "C" uae_u32 Uae2026JitLastLowpcFaultOpcode;
extern "C" uae_u32 Uae2026JitLastCodeHostPc;
extern "C" uae_u32 Uae2026JitLastCodeHostPhys;
extern "C" uae_u32 Uae2026JitLastCodeHostWords[12];
extern "C" uae_u32 Uae2026JitCodeHostRingSeq;
extern "C" uae_u32 Uae2026JitCodeHostRingPc[64];
extern "C" uae_u32 Uae2026JitCodeHostRingPhys[64];
extern "C" uae_u32 Uae2026JitCodeHostRingWords[64][12];
extern "C" uae_u32 Uae2026JitCodeHostRingRegs[64][16];
extern "C" uae_u32 Uae2026JitCodeHostRingSr[64];
extern "C" uae_u32 Uae2026JitLastExceptionSp;
extern "C" struct flag_struct Uae2026JitLastFlags;

/* regflags — CPU flag struct (Previous layout: cznv field). */
extern struct flag_struct regflags;

/* jit_regflags — the JIT's AArch64 flag struct (nzcv/x).      *
 * Defined in uae2026_compiler_unit.cpp as a renamed 'regflags'. */
struct jit_flag_struct { uae_u32 nzcv; uae_u32 x; };
extern struct jit_flag_struct jit_regflags;

namespace {
enum class bridge_jit_helper_phase : uae_u32 {
    none = 0,
    pre_semantic = 1,
    semantic_committed = 2,
    target_fetch_committed = 3
};

struct bridge_jit_helper_state {
    bool active;
    uae_u16 kind;
    uae_u16 opcode;
    uae_u16 instruction_bytes;
    uae_u32 op_pc;
    uae_u16 pre_sr;
    uae_u32 pre_a7;
    uae_u32 pre_jit_nzcv;
    uae_u32 pre_jit_x;
    uae_u32 logical_next_pc;
    uintptr_t translated_next_host;
    uae_u32 flag_authority;
    bridge_jit_helper_phase phase;
};

static bridge_jit_helper_state bridge_helper_state{};
static uae_u32 mmu_translation_generation = 1;
static bool compiler_initialized = false;

/* Bump census, by the register write that caused it. */
enum { MMU_CHANGE_SOURCES = 12 };
struct mmu_change_source { uae_u32 source; unsigned long count; };
static mmu_change_source mmu_change_by_source[MMU_CHANGE_SOURCES]{};
static unsigned long mmu_change_count = 0;
}

/* Bit-position conversion between Previous's legacy flag layout and the
 * vendored UAE2026 JIT layout:
 *
 *   Previous legacy (src/cpu/newcpu.h):            N=15 Z=14 C=8  V=0  X=8
 *   UAE2026 AArch64 (src/cpu/uae_cpu_2026/m68k.h): N=31 Z=30 C=29 V=28 X=29
 *
 * The structs have the same two-word shape but no raw-copy ABI. Use these
 * helpers at every JIT<->legacy boundary, including restart snapshots. */
static inline uae_u32 bridge_cznv_legacy_to_jit(uae_u32 legacy)
{
    /* Legacy Previous (src/cpu/newcpu.h) stores CCR in cznv with
     *   N=15  Z=14  C=8   V=0
     * AArch64 JIT (src/cpu/uae_cpu_2026/m68k.h) stores NZCV with
     *   N=31  Z=30  C=29  V=28
     * (matching the host ARM64 NZCV system register). */
    uae_u32 jit = 0;
    if (legacy & (1u << 15)) jit |= (1u << 31); /* N */
    if (legacy & (1u << 14)) jit |= (1u << 30); /* Z */
    if (legacy & (1u << 8))  jit |= (1u << 29); /* C */
    if (legacy & (1u << 0))  jit |= (1u << 28); /* V */
    return jit;
}

static inline uae_u32 bridge_cznv_jit_to_legacy(uae_u32 jit)
{
    uae_u32 legacy = 0;
    if (jit & (1u << 31)) legacy |= (1u << 15); /* N */
    if (jit & (1u << 30)) legacy |= (1u << 14); /* Z */
    if (jit & (1u << 29)) legacy |= (1u << 8);  /* C */
    if (jit & (1u << 28)) legacy |= (1u << 0);  /* V */
    return legacy;
}

static inline uae_u32 bridge_x_legacy_to_jit(uae_u32 legacy)
{
    return (legacy & (1u << 8)) ? (1u << 29) : 0;
}

static inline uae_u32 bridge_x_jit_to_legacy(uae_u32 jit)
{
    return (jit & (1u << 29)) ? (1u << 8) : 0;
}

extern "C" uae_u32 Uae2026BridgeCznvLegacyToJit(uae_u32 v) { return bridge_cznv_legacy_to_jit(v); }
extern "C" uae_u32 Uae2026BridgeCznvJitToLegacy(uae_u32 v) { return bridge_cznv_jit_to_legacy(v); }

/* Inline compiler fallbacks call Previous's separately compiled cpuemu handlers.
 * Publish flags in that translation unit's legacy layout before the call, then
 * import its result into the ARM64-native JIT layout afterwards.  The two
 * structs deliberately share no raw-copy ABI: CZNV uses 15/14/8/0 versus
 * 31/30/29/28, and X uses bit 8 versus bit 29. */
extern "C" void Uae2026JitFlagsToInterpreter(void)
{
    regflags.cznv = bridge_cznv_jit_to_legacy(jit_regflags.nzcv);
    regflags.x = bridge_x_jit_to_legacy(jit_regflags.x);
}

extern "C" void Uae2026InterpreterFlagsToJit(void)
{
    jit_regflags.nzcv = bridge_cznv_legacy_to_jit(regflags.cznv);
    jit_regflags.x = bridge_x_legacy_to_jit(regflags.x);
}

static inline uae_u16 bridge_sr_with_jit_flags(uae_u16 sr)
{
    const uae_u32 legacy = bridge_cznv_jit_to_legacy(jit_regflags.nzcv);
    const uae_u16 ccr = (uae_u16)((((jit_regflags.x >> 29) & 1u) << 4) |
        (((legacy >> 15) & 1u) << 3) |
        (((legacy >> 14) & 1u) << 2) |
        (((legacy >> 0) & 1u) << 1) |
        ((legacy >> 8) & 1u));
    return (uae_u16)((sr & 0xffe0u) | ccr);
}

/* The condition codes as the JIT's live shadow holds them.
 *
 * Exception frames are built from regs.sr, which MakeSR() materialises from
 * Previous's regflags.  Compiled code keeps the live flags in jit_regflags and
 * only publishes them into regflags at certain seams, so a frame pushed at an
 * exception entry that missed one of those seams would carry stale condition
 * codes.  Exporting the shadow's view lets the exception stream carry both and
 * settle whether that ever happens. */
extern "C" uae_u32 Uae2026JitCcr(void)
{
    return (uae_u32)(bridge_sr_with_jit_flags(0) & 0x1fu);
}

extern "C" void Uae2026JitHelperClear(void)
{
    bridge_helper_state = {};
}

/* Interpreter-fallback census.
 *
 * "Is the JIT running 100% natively?" cannot be answered from the compile-time
 * JIT_FALLBACK log: it is capped, it counts compilations rather than
 * executions, and it says nothing about how much guest work each fallback
 * family actually does.  Every fallback execution passes through
 * Uae2026JitHelperBegin with kind EXACT_OPCODE, so counting there is uncapped,
 * always available and weighted by execution.  B2_JIT_HELPER_CENSUS=<n> dumps
 * the histogram every n fallback executions (default 20000000).
 */
static unsigned long long bridge_helper_census_ops[2][65536];
static unsigned long long bridge_helper_census_site[4];
static unsigned long long bridge_helper_census_kind[8];
static unsigned long long bridge_helper_census_total = 0;

static unsigned long long bridge_helper_census_every(void)
{
    static long long cached = -1;
    if (cached < 0) {
        const char *env = getenv("B2_JIT_HELPER_CENSUS");
        if (!env || !*env || !strcmp(env, "0"))
            cached = 0;
        else {
            long long v = strtoll(env, NULL, 0);
            cached = (v > 1) ? v : 20000000ll;
        }
    }
    return (unsigned long long)cached;
}

static void bridge_helper_census_top(int site, const char *label)
{
    enum { TOP = 20 };
    unsigned top[TOP];
    int found = 0;
    for (int slot = 0; slot < TOP; slot++) {
        unsigned long long best = 0;
        unsigned best_op = 0;
        for (unsigned op = 0; op < 65536u; op++) {
            if (!bridge_helper_census_ops[site][op])
                continue;
            bool taken = false;
            for (int k = 0; k < slot; k++)
                if (top[k] == op) { taken = true; break; }
            if (taken)
                continue;
            if (bridge_helper_census_ops[site][op] > best) {
                best = bridge_helper_census_ops[site][op];
                best_op = op;
            }
        }
        if (!best)
            break;
        top[slot] = best_op;
        found = slot + 1;
    }
    for (int slot = 0; slot < found; slot++)
        fprintf(stderr, "JITHELPEROP %s op=%04x n=%llu\n",
            label, top[slot], bridge_helper_census_ops[site][top[slot]]);
}

static void bridge_helper_census_dump(const char *tag)
{
    extern unsigned long jit_retire_obs[4];
    extern unsigned long jit_stat_dispatch;
    extern unsigned long jit_stat_compile;
    extern int64_t nCyclesMainCounter;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(stderr, "JITHELPERCENSUS tag=%s active=%d wall=%lld.%03ld total=%llu inblock=%llu trace=%llu "
        "nostats=%llu nostats_lim=%llu kinds=%llu/%llu/%llu/%llu/%llu/%llu "
        "obs=%lu/%lu/%lu/%lu disp=%lu comp=%lu cyc=%lld\n",
        tag ? tag : "periodic", Uae2026JitBridgeIsActive() ? 1 : 0,
        (long long)ts.tv_sec, ts.tv_nsec / 1000000,
        bridge_helper_census_total,
        bridge_helper_census_site[0], bridge_helper_census_site[1],
        bridge_helper_census_site[2], bridge_helper_census_site[3],
        bridge_helper_census_kind[0], bridge_helper_census_kind[1],
        bridge_helper_census_kind[2], bridge_helper_census_kind[3],
        bridge_helper_census_kind[4], bridge_helper_census_kind[5],
        jit_retire_obs[0], jit_retire_obs[1], jit_retire_obs[2],
        jit_retire_obs[3],
        jit_stat_dispatch, jit_stat_compile, (long long)nCyclesMainCounter);
    bridge_helper_census_top(0, "inblock");
    bridge_helper_census_top(1, "trace");
    {
        extern unsigned long jit_ident_ok, jit_ident_rej_ident, jit_ident_rej_gen,
            jit_ident_rej_gen_1page, jit_ident_rej_gen_1page4k, jit_ident_exempt_1page;
        extern unsigned long jit_icache_flush_deferred, jit_icache_flush_immediate;
        extern unsigned long jit_shadow_sync_hits, jit_shadow_sync_copies;
        extern unsigned long jit_ident_exempt_1page_csumfail;
        extern unsigned long jit_need_check_revalidated, jit_need_check_discarded;
        extern unsigned long jit_mmu_fast_dispatch_hit, jit_mmu_fast_dispatch_miss;
        fprintf(stderr, "JITIDENT ok=%lu rej_ident=%lu rej_gen=%lu gen1p8k=%lu "
            "gen1p4k=%lu exempt1p=%lu gen=%u bumps=%lu icdefer=%lu icflush=%lu "
            "shhit=%lu shcopy=%lu csfail=%lu reval=%lu revdisc=%lu "
            "fast_hit=%lu fast_miss=%lu",
            jit_ident_ok, jit_ident_rej_ident, jit_ident_rej_gen,
            jit_ident_rej_gen_1page, jit_ident_rej_gen_1page4k,
            jit_ident_exempt_1page,
            mmu_translation_generation, mmu_change_count,
            jit_icache_flush_deferred, jit_icache_flush_immediate,
            jit_shadow_sync_hits, jit_shadow_sync_copies,
            jit_ident_exempt_1page_csumfail,
            jit_need_check_revalidated, jit_need_check_discarded,
            jit_mmu_fast_dispatch_hit, jit_mmu_fast_dispatch_miss);
        for (int i = 0; i < MMU_CHANGE_SOURCES; i++)
            if (mmu_change_by_source[i].count)
                fprintf(stderr, " %08x=%lu", mmu_change_by_source[i].source,
                    mmu_change_by_source[i].count);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

/* site 0 = interpreter fallback inside a compiled block (the instruction has
 * no native handler), 1 = first-pass tracer, 2 = exec_nostats, 3 = the bounded
 * exec_nostats.  Only site 0 means "compiled code could not do this natively";
 * the others mean "this block was not running as compiled code at all". */
extern "C" void Uae2026JitFallbackCensus(uae_u32 opcode, uae_u32 site)
{
    if (!bridge_helper_census_every())
        return;
    bridge_helper_census_site[site & 3u]++;
    if (site < 2u)
        bridge_helper_census_ops[site][opcode & 0xffffu]++;
    if ((++bridge_helper_census_total % bridge_helper_census_every()) == 0)
        bridge_helper_census_dump("periodic");
}

extern "C" void Uae2026JitTimingAnchorReport(void)
{
    bridge_helper_census_dump("timing-anchor");
    Uae2026JitDiagnosticReport();
}

extern "C" void Uae2026JitBenchmarkReport(void)
{
    const char *env = getenv("B2_JIT_BENCH_REPORT");
    if (!(env && *env && strcmp(env, "0")))
        return;
    bridge_helper_census_dump("final");
    {
        extern unsigned long jit_retire_obs[4];
        const char *expected_env = getenv("B2_JIT_BENCH_EXPECTED_INSNS");
        const unsigned long long expected = (expected_env && *expected_env)
            ? strtoull(expected_env, NULL, 0) : 0;
        const unsigned long long observed =
            (unsigned long long)jit_retire_obs[0] + jit_retire_obs[1] +
            jit_retire_obs[2] + jit_retire_obs[3];
        const bool active = Uae2026JitBridgeIsActive();
        /* The interpreter loop recognizes the synthetic STOP before invoking
         * its retirement observer.  The JIT first-pass tracer observes STOP,
         * then invokes the same trailer service.  Reconcile that intentional
         * callback asymmetry explicitly instead of calling it skipped work. */
        const unsigned long long stop_unobserved =
            (!active && expected == observed + 1) ? 1 : 0;
        const bool reconciled = expected != 0 &&
            observed + stop_unobserved == expected;
        const unsigned long long native_ppm = observed && active
            ? ((unsigned long long)jit_retire_obs[0] * 1000000ULL) / observed : 0;
        const unsigned long long trace_ppm = observed && active
            ? ((unsigned long long)jit_retire_obs[2] * 1000000ULL) / observed : 0;
        fprintf(stderr,
            "JITBENCHCOVERAGE active=%d architectural=%llu observed=%llu "
            "stop_unobserved=%llu reconciled=%d paths=%lu/%lu/%lu/%lu "
            "native_ppm=%llu trace_ppm=%llu\n",
            active ? 1 : 0, expected, observed, stop_unobserved,
            reconciled ? 1 : 0, jit_retire_obs[0], jit_retire_obs[1],
            jit_retire_obs[2], jit_retire_obs[3], native_ppm, trace_ppm);
    }
    Uae2026JitDiagnosticReport();
}

extern "C" void Uae2026JitHelperBegin(uae_u32 op_pc, uae_u32 descriptor)
{
    if (bridge_helper_state.active)
        jit_abort("nested JIT semantic helper old=%08x/%04x new=%08x/%04x",
            bridge_helper_state.op_pc, bridge_helper_state.opcode,
            op_pc, descriptor & 0xffffu);

    bridge_helper_state.active = true;
    if (bridge_helper_census_every())
        bridge_helper_census_kind[((descriptor >> 16) & 0xffu) & 7u]++;
    bridge_helper_state.kind = (uae_u16)((descriptor >> 16) & 0xffu);
    bridge_helper_state.opcode = (uae_u16)descriptor;
    bridge_helper_state.instruction_bytes = (uae_u16)(descriptor >> 24);
    bridge_helper_state.op_pc = op_pc;
    bridge_helper_state.pre_sr = regs.sr;
    bridge_helper_state.pre_a7 = m68k_areg(regs, 7);
    bridge_helper_state.pre_jit_nzcv = jit_regflags.nzcv;
    bridge_helper_state.pre_jit_x = jit_regflags.x;
    bridge_helper_state.flag_authority = UAE2026_JIT_FLAGS_ARE_JIT;
    bridge_helper_state.phase = bridge_jit_helper_phase::pre_semantic;

    /* Callers establish the complete logical/direct PC tuple before begin.
     * Rewriting only regs.pc here double-counts any live pc_p-pc_oldp delta
     * in exec_nostats and turns a valid successor into a shifted logical PC. */
    regs.fault_pc = op_pc;
    regs.instruction_pc = op_pc;
    regs.mmu_effective_addr = 0;
    Uae2026JitLastInstructionPc = op_pc;
    Uae2026JitLastSr = bridge_sr_with_jit_flags(regs.sr);
    Uae2026JitLastA7 = m68k_areg(regs, 7);
    /* Restart snapshots stay in their producer's JIT layout. The catch
     * boundary converts both words before restoring Previous's regflags. */
    Uae2026JitLastFlags.cznv = jit_regflags.nzcv;
    Uae2026JitLastFlags.x = jit_regflags.x;
    mmu_restart = true;
    mmu_opcode = bridge_helper_state.opcode;
}

extern "C" uae_u32 Uae2026JitMmuGeneration(void)
{
    return mmu_translation_generation;
}

extern "C" uintptr_t Uae2026JitMmuGenerationAddress(void)
{
    return (uintptr_t)&mmu_translation_generation;
}

extern "C" void Uae2026JitPrepareContinuationWrite(uae_u32 post_pc)
{
    /* Generated 68040 destination-write handlers publish the successor and
       clear restart/fixup state immediately before the faultable bus cycle. */
    regs.fault_pc = post_pc;
    regs.instruction_pc = post_pc;
    Uae2026JitLastInstructionPc = post_pc;
    mmu_restart = false;
    mmufixup[0].reg = -1;
    mmufixup[1].reg = -1;
}

extern "C" void Uae2026JitMmuTranslationChanged(uae_u32 source)
{
    /* Startup/reset calls made before compiler_init() have no compiled identity
     * to invalidate. The first live JIT context starts at generation one. */
    if (!compiler_initialized)
        return;

    /* Which register write caused the bump.  Per-page invalidation can only
     * help the sources that name a page (PFLUSH of one entry); PFLUSHA, a TTR
     * write and a root-pointer write name none. */
    for (int i = 0; i < MMU_CHANGE_SOURCES; i++) {
        if (mmu_change_by_source[i].source == source ||
            mmu_change_by_source[i].count == 0) {
            mmu_change_by_source[i].source = source;
            mmu_change_by_source[i].count++;
            break;
        }
    }
    mmu_change_count++;

    /* Generation zero is reserved for non-JIT stubs. Wrapping is practically
     * unreachable, but skip it so no live MMU block can acquire the sentinel. */
    if (++mmu_translation_generation == 0)
        mmu_translation_generation = 1;

    /* Invalidation policy. The original threw away every compiled translation
     * on each ATC flush. The keying half of that policy IS required (see
     * jit_mmu_generation_keying_enabled(): without it a page unmapped for demand
     * paging can be re-entered through its old block and the fault is skipped),
     * but the whole-cache HARD flush is not, and it is ruinously expensive:
     * NeXTSTEP Mach PFLUSHes ~1.5k/s, which measured 26M block compiles in 280s.
     * Use the lazy flush -- it re-arms per-block checksums and keeps the code
     * cache -- and let the generation key handle correctness. Set
     * PREVIOUS_UAE2026_JIT_MMU_HARD_FLUSH=1 to restore the old behaviour.
     * flush_icache_hard() also sets SPCFLAG_JIT_EXEC_RETURN, terminating an
     * active native block before its next fetch or memory operation. */
    static int hard_flush_env = -1;
    if (hard_flush_env < 0) {
        const char *env = getenv("PREVIOUS_UAE2026_JIT_MMU_HARD_FLUSH");
        hard_flush_env = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    if (hard_flush_env)
        Uae2026CompilerFlushCacheHard();
    else
        Uae2026CompilerFlushCacheLazy();

    static unsigned long change_count = 0;
    if (++change_count <= 16 || (change_count % 1024) == 0)
        fprintf(stderr,
            "UAE2026 bridge: MMU translation generation=%u source=%08x count=%lu\n",
            mmu_translation_generation, source, change_count);
}

extern "C" uintptr_t Uae2026JitPrepareMmuDispatchTarget(uae_u32 logical_pc)
{
    /* Used by compiled constant edges. Translation is faultable, so callers
     * publish the logical target before entering. Keep the architectural PC
     * distinct from the translated executable-shadow address. */
    regs.pc = logical_pc;
    regs.fault_pc = logical_pc;
    regs.instruction_pc = logical_pc;
    Uae2026JitLastInstructionPc = logical_pc;
    mmu_restart = true;
    mmu_opcode = 0xffff;
    const uintptr_t host = Uae2026JitMmuXlateCodeHost(logical_pc);
    regs.pc_p = (uae_u8 *)host;
    regs.pc_oldp = regs.pc_p;
    return host;
}

extern "C" void Uae2026JitHelperCommitCurrentPc(void)
{
    if (!bridge_helper_state.active)
        return; /* An exact helper already committed and cleared its outcome. */
    if (bridge_helper_state.phase != bridge_jit_helper_phase::pre_semantic)
        jit_abort("JIT semantic helper invalid post-call phase %u",
            (unsigned)bridge_helper_state.phase);

    /* Direct-address helper code advances pc_p. If it did not explicitly write
     * regs.pc, the only valid logical successor is the same opcode-relative
     * byte delta. This uses the known pre-helper tuple, never MEMBaseDiff. */
    uae_u32 logical_pc = regs.pc;
    if (logical_pc == bridge_helper_state.op_pc && regs.pc_p && regs.pc_oldp)
        logical_pc = bridge_helper_state.op_pc +
            (uae_s32)((intptr_t)regs.pc_p - (intptr_t)regs.pc_oldp);
    Uae2026JitHelperCommitLogicalPc(logical_pc, UAE2026_JIT_FLAGS_ARE_JIT);
}

extern "C" void Uae2026JitHelperCommitArchitecturalPc(void)
{
    if (!bridge_helper_state.active)
        return;
    if (bridge_helper_state.phase != bridge_jit_helper_phase::pre_semantic)
        jit_abort("JIT semantic helper invalid architectural-PC phase %u",
            (unsigned)bridge_helper_state.phase);

    /* Native Previous helpers can advance both halves of the direct-PC tuple:
     * instruction words move pc_p while 68040 MMU operand fetches move regs.pc.
     * The architectural successor is therefore m68k_getpc(), not either field
     * in isolation. This policy is selected explicitly by those helpers. */
    Uae2026JitHelperCommitLogicalPc(m68k_getpc(),
        UAE2026_JIT_FLAGS_ARE_JIT);
}

extern "C" void Uae2026JitHelperCommitLogicalPc(uae_u32 logical_pc, uae_u32 flag_authority)
{
    if (!bridge_helper_state.active ||
        bridge_helper_state.phase != bridge_jit_helper_phase::pre_semantic)
        jit_abort("JIT semantic helper commit without begin pc=%08x", logical_pc);

    bridge_helper_state.logical_next_pc = logical_pc;
    bridge_helper_state.flag_authority = flag_authority;
    bridge_helper_state.phase = bridge_jit_helper_phase::semantic_committed;

    if (flag_authority == UAE2026_JIT_FLAGS_ARE_PREVIOUS) {
        jit_regflags.nzcv = bridge_cznv_legacy_to_jit(regflags.cznv);
        jit_regflags.x = bridge_x_legacy_to_jit(regflags.x);
    } else if (flag_authority == UAE2026_JIT_FLAGS_ARE_JIT) {
        /* Canonical helper exits deliberately return through execute_normal in
         MMU mode.  The next opcode can therefore be an interpreter fallback,
         whose condition-code macros read Previous's separate regflags symbol.
         Mirror the authoritative JIT result now; waiting for the outer bridge
         return lets that fallback consume the pre-helper CCR. */
        regflags.cznv = bridge_cznv_jit_to_legacy(jit_regflags.nzcv);
        regflags.x = bridge_x_jit_to_legacy(jit_regflags.x);
    } else {
        jit_abort("invalid JIT semantic helper flag authority %u", flag_authority);
    }

    /* Publish the semantic result before target translation. If translation
     * faults, retain the committed SR/A7/CCR and never repeat the helper's
     * frame or stack operation. */
    regs.pc = logical_pc;
    regs.fault_pc = logical_pc;
    regs.instruction_pc = logical_pc;
    regs.mmu_effective_addr = 0;
    Uae2026JitLastInstructionPc = logical_pc;
    Uae2026JitLastSr = bridge_sr_with_jit_flags(regs.sr);
    Uae2026JitLastA7 = m68k_areg(regs, 7);
    Uae2026JitLastFlags.cznv = jit_regflags.nzcv;
    Uae2026JitLastFlags.x = jit_regflags.x;
    mmu_restart = true;
    mmu_opcode = 0xffff;

    const uintptr_t host = Uae2026JitMmuXlateCodeHost(logical_pc);
    bridge_helper_state.translated_next_host = host;
    bridge_helper_state.phase = bridge_jit_helper_phase::target_fetch_committed;
    regs.pc = logical_pc;
    regs.pc_p = (uae_u8 *)host;
    regs.pc_oldp = regs.pc_p;
    bridge_helper_state = {};
}

/* JIT lockstep tracer support: read/seed the REAL interpreter `regflags`
 * (legacy cznv layout) in canonical M68K CCR layout (bit4=X bit3=N bit2=Z
 * bit1=V bit0=C). The lockstep gold step runs interpreter handlers that write
 * THIS regflags symbol, but the JIT compiler unit's `regflags` is the renamed
 * jit_regflags (nzcv) with a different struct layout, so the gold flag state
 * must be bridged through these interpreter-unit accessors instead of a raw
 * cross-layout struct copy. */
extern "C" uae_u8 Uae2026InterpCanonicalCcr5(void)
{
    return (uae_u8)(((GET_XFLG() & 1) << 4) | ((GET_NFLG() & 1) << 3) |
                    ((GET_ZFLG() & 1) << 2) | ((GET_VFLG() & 1) << 1) | (GET_CFLG() & 1));
}
extern "C" void Uae2026InterpSeedCcr5(uae_u8 ccr)
{
    SET_XFLG((ccr >> 4) & 1);
    SET_NFLG((ccr >> 3) & 1);
    SET_ZFLG((ccr >> 2) & 1);
    SET_VFLG((ccr >> 1) & 1);
    SET_CFLG(ccr & 1);
}

namespace {
static bool bridge_logged = false;
static char bridge_summary[768];
static bool bootstrap_attempted = false;
static bool bootstrap_active = false;
static bool compiler_prefs_applied = false;
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

/* Every fault-repair decision in this file decodes the faulting instruction's
 * own words, and Uae2026JitLiveGetWord() is a *physical* reader.  A user PC
 * such as 0x05007226 is inside the RAM window when read as a physical address,
 * so the peek silently returns an unrelated word -- zero, in the measured case
 * -- and every opcode-gated repair is skipped (or, worse, taken on a
 * misdecoded opcode) for MMU-translated user space.  This is the same
 * PC-representation seam as fb76772 (get_iword) and ceea2d0 (indexed EAs), on
 * the fault-repair side.
 *
 * Translate through a read-only ATC probe: the faulting instruction was just
 * fetched, so its page is resident; the probe never fills the ATC and never
 * raises a nested bus error, so it cannot disturb the exception about to be
 * delivered.  A miss falls back to the historical physical reading, which
 * leaves identity-mapped ROM and kernel text behaving exactly as before. */
/* Resolve a guest PC to a physical address that is safe to read with the
 * *physical* live reader.  Two outcomes are safe: the ATC probe supplied a
 * real translation, or the MMU is off and the address is already physical.
 *
 * The third case -- MMU on, probe misses -- is not.  The historical fallback
 * read the untranslated logical address, and a low user PC such as 0x000323fa
 * passes bridge_live_readable() through the sub-0x40000 window while the
 * corresponding *physical* page is unmapped, so wordget() reaches the dummy
 * bank and calls M68000_BusError().  That turns a decision-support peek into a
 * delivered guest exception: exception2() -> mmu_bus_error(nonmmu=true) with
 * fc=user-data and size=byte, i.e. SSW=0x0121 instead of the code-fetch page
 * fault the interpreter reports (SSW=0x0542), and the kernel then handles a
 * text page-in as a hard bus error.  A peek must never be able to fault.
 *
 * On a probe miss the fallback is therefore restricted to the address ranges
 * this machine maps identically and always backs with memory -- ROM and RAM --
 * which is exactly the set the fallback existed to preserve (identity-mapped
 * ROM and kernel text); everything else reports "not readable". */
static bool bridge_code_phys_ok(uae_u32 pc, uae_u32 *out_phys)
{
    uaecptr phys = pc;
    if (regs.mmu_enabled) {
        if (mmu_probe_atc((uaecptr)pc, regs.s != 0, &phys)) {
            *out_phys = (uae_u32)phys;
            return true;
        }
        const uae_u32 canon = bridge_live_canonical_addr(pc);
        if (!((canon >= 0x01000000u && canon < 0x01020000u) ||
              (canon >= 0x04000000u && canon < 0x08000000u)))
            return false;
    }
    *out_phys = pc;
    return true;
}

static bool bridge_code_readable(uae_u32 pc, uae_u32 bytes)
{
    uae_u32 phys = pc;
    if (!bridge_code_phys_ok(pc, &phys))
        return false;
    return bridge_live_readable(phys, bytes);
}

static uae_u32 bridge_peek_code_word(uae_u32 pc)
{
    uae_u32 phys = pc;
    if (!bridge_code_phys_ok(pc, &phys))
        return 0;
    return bridge_live_peek_word(phys);
}

static uae_u32 bridge_live_peek_long(uae_u32 addr)
{
    return bridge_live_readable(addr, 4) ? bridge_live_peek_word(addr) << 16 | bridge_live_peek_word(addr + 2) : 0;
}

/* Diagnostic: read a guest long through the same read-only ATC probe the
 * fault-repair peeks use, so an instrument can look at guest data (a return
 * address on the user stack, say) without being able to raise a fault or
 * disturb the ATC.  Probes each word separately: a long may straddle a page
 * boundary whose second page is not resident. */
extern "C" bool Uae2026JitSafePeekWord(uae_u32 addr, uae_u32 *out)
{
    uae_u32 phys = addr;
    if (regs.mmu_enabled) {
        uaecptr p = 0;
        if (!mmu_probe_atc((uaecptr)addr, regs.s != 0, &p))
            return false;
        phys = (uae_u32)p;
    }
    if (!bridge_live_readable(phys, 2))
        return false;
    *out = Uae2026JitLiveGetWord(bridge_live_canonical_addr(phys));
    return true;
}

extern "C" bool Uae2026JitSafePeekLong(uae_u32 addr, uae_u32 *out)
{
    uae_u32 hi = 0, lo = 0;
    if (!Uae2026JitSafePeekWord(addr, &hi) || !Uae2026JitSafePeekWord(addr + 2, &lo))
        return false;
    *out = (hi << 16) | (lo & 0xffffu);
    return true;
}

static bool bridge_mmio_addr(uae_u32 addr)
{
    return addr >= 0x02000000u && addr < 0x02200000u;
}

static bool bridge_try_handle_mmio_byte_op(void)
{
    const uae_u32 addr = regs.mmu_fault_addr;
    if (!bridge_mmio_addr(addr) || regs.fault_pc == 0)
        return false;

    /* Generic NeXT MMIO model for native paths that still bypass the JIT
     * bank helpers and fault on direct shadow memory.  Do not hand-decode
     * per-PC instructions here: restore the architectural PC and delegate the
     * single faulting opcode to Previous's normal interpreter function table,
     * whose byte/word/long accesses already go through the live addrbanks. */
    const uae_u32 pc = regs.fault_pc;
    const uae_u16 opcode = (uae_u16)bridge_peek_code_word(pc);
    cpuop_func *handler = cpufunctbl[opcode];
    if (!handler)
        return false;

    regs.fault_pc = 0;
    regs.mmu_fault_addr = 0;
    regs.mmu_effective_addr = 0;
    regs.instruction_pc = pc;
    mmu_restart = false;
    mmu_opcode = 0xffffu;
    m68k_setpc(pc);
    handler(opcode);
    jit_regflags.nzcv = bridge_cznv_legacy_to_jit(regflags.cznv);
    jit_regflags.x = bridge_x_legacy_to_jit(regflags.x);
    return true;
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

extern "C" unsigned long Uae2026JitInterpResumeCountdown;

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

static bool bridge_trace_fault_words_in_range(uae_u32 value, uae_u32 start, uae_u32 end)
{
    return value >= start && value <= end;
}

static void bridge_trace_fault_words_words(const char *tag, unsigned long n, uae_u32 base)
{
    fprintf(stderr, "JIT_FAULT_%s n=%lu base=%08x", tag, n, (unsigned)base);
    for (unsigned wi = 0; wi < 12; wi++) {
        const uae_u32 addr = base + wi * 2u;
        fprintf(stderr, " w%u=%04x", wi,
                bridge_live_readable(addr, 2) ? (unsigned)Uae2026JitLiveGetWord(addr) : 0xffffu);
    }
    fprintf(stderr, "\n");
}

static bool bridge_fault_frame_user_stack_addr(uae_u32 addr)
{
    return addr >= 0x03000000u && addr < 0x04000000u;
}

static uae_u32 bridge_fault_frame_peek_long(uae_u32 addr)
{
    /* The current high-user frontier's A6/A7 frame lives in user virtual stack
     * space.  The direct live/physical view is often zero there; use the 040
     * data translation only when explicitly requested so the normal diagnostic
     * stays non-faulting and conservative by default. */
    if (env_truthy("B2_JIT_TRACE_FAULT_FRAME_MMU", false) && regs.mmu_enabled &&
        bridge_fault_frame_user_stack_addr(addr)) {
        const uae_u32 phys = Uae2026JitMmuXlateData(addr);
        if (bridge_live_readable(phys, 4))
            return bridge_live_peek_long(phys);
    }
    if (bridge_live_readable(addr, 4))
        return bridge_live_peek_long(addr);
    /* User stacks in the failing NeXTSTEP paths live near 0x03ffxxxx and are
     * usually backed by the top of the 64MiB RAM shadow, but some paths require
     * real MMU translation above.  Keep this fallback local to the opt-in frame
     * diagnostic so normal bridge/live peeks retain their conservative behavior. */
    if (bridge_fault_frame_user_stack_addr(addr)) {
        const uae_u32 ram_addr = 0x04000000u | addr;
        if (bridge_live_readable(ram_addr, 4))
            return bridge_live_peek_long(ram_addr);
    }
    return 0;
}

static void bridge_trace_codehost_frame(unsigned long n)
{
    if (!env_truthy("B2_JIT_TRACE_FAULT_FRAME", false))
        return;
    const uae_u32 a6 = regs.regs[14];
    const uae_u32 a7 = regs.regs[15];
    const uae_u32 caller_fp = bridge_fault_frame_peek_long(a6);
    const uae_u32 caller2_fp = bridge_fault_frame_peek_long(caller_fp);
    const uae_u32 argp = bridge_fault_frame_peek_long(a6 + 12u);
    fprintf(stderr,
            "JIT_FAULT_FRAME n=%lu pc=%08x fault_pc=%08x a6=%08x a7=%08x caller_fp=%08x caller2_fp=%08x argp=%08x arg_last=%08x "
            "a6_m16=%08x a6_m12=%08x a6_m8=%08x a6_m4=%08x a6_p0=%08x a6_p4=%08x a6_p8=%08x a6_p12=%08x a6_p16=%08x a6_p20=%08x "
            "caller_p0=%08x caller_p4=%08x caller_p8=%08x caller_p12=%08x caller_p16=%08x caller_p20=%08x caller_p24=%08x "
            "caller2_p0=%08x caller2_p4=%08x caller2_p8=%08x caller2_p12=%08x caller2_p16=%08x caller2_p20=%08x caller2_p24=%08x "
            "a7_p0=%08x a7_p4=%08x a7_p8=%08x a7_p12=%08x arg_m16=%08x arg_m12=%08x arg_m8=%08x arg_m4=%08x arg_p0=%08x arg_p4=%08x arg_p8=%08x arg_p12=%08x\n",
            n, (unsigned)m68k_getpc(), (unsigned)regs.fault_pc,
            (unsigned)a6, (unsigned)a7, (unsigned)caller_fp, (unsigned)caller2_fp, (unsigned)argp,
            (unsigned)(argp >= 4 ? bridge_fault_frame_peek_long(argp - 4u) : 0),
            bridge_fault_frame_peek_long(a6 - 16u), bridge_fault_frame_peek_long(a6 - 12u),
            bridge_fault_frame_peek_long(a6 - 8u), bridge_fault_frame_peek_long(a6 - 4u),
            bridge_fault_frame_peek_long(a6), bridge_fault_frame_peek_long(a6 + 4u),
            bridge_fault_frame_peek_long(a6 + 8u), bridge_fault_frame_peek_long(a6 + 12u),
            bridge_fault_frame_peek_long(a6 + 16u), bridge_fault_frame_peek_long(a6 + 20u),
            bridge_fault_frame_peek_long(caller_fp), bridge_fault_frame_peek_long(caller_fp + 4u),
            bridge_fault_frame_peek_long(caller_fp + 8u), bridge_fault_frame_peek_long(caller_fp + 12u),
            bridge_fault_frame_peek_long(caller_fp + 16u), bridge_fault_frame_peek_long(caller_fp + 20u),
            bridge_fault_frame_peek_long(caller_fp + 24u),
            bridge_fault_frame_peek_long(caller2_fp), bridge_fault_frame_peek_long(caller2_fp + 4u),
            bridge_fault_frame_peek_long(caller2_fp + 8u), bridge_fault_frame_peek_long(caller2_fp + 12u),
            bridge_fault_frame_peek_long(caller2_fp + 16u), bridge_fault_frame_peek_long(caller2_fp + 20u),
            bridge_fault_frame_peek_long(caller2_fp + 24u),
            bridge_fault_frame_peek_long(a7), bridge_fault_frame_peek_long(a7 + 4u),
            bridge_fault_frame_peek_long(a7 + 8u), bridge_fault_frame_peek_long(a7 + 12u),
            bridge_fault_frame_peek_long(argp - 16u), bridge_fault_frame_peek_long(argp - 12u),
            bridge_fault_frame_peek_long(argp - 8u), bridge_fault_frame_peek_long(argp - 4u),
            bridge_fault_frame_peek_long(argp), bridge_fault_frame_peek_long(argp + 4u),
            bridge_fault_frame_peek_long(argp + 8u), bridge_fault_frame_peek_long(argp + 12u));
}

static void bridge_trace_codehost_ring(unsigned long n)
{
    if (!env_truthy("B2_JIT_TRACE_CODEHOST_RING", false))
        return;
    const uae_u32 seq = Uae2026JitCodeHostRingSeq;
    const uae_u32 valid = seq < 64u ? seq : 64u;
    for (uae_u32 oi = 0; oi < valid; oi++) {
        const uae_u32 slot = (seq - valid + oi) & 63u;
        fprintf(stderr,
                "JIT_FAULT_CODEHOST_RING n=%lu i=%u slot=%u pc=%08x phys=%08x sr=%04x "
                "d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x "
                "a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x a7=%08x",
                n, (unsigned)oi, (unsigned)slot,
                (unsigned)Uae2026JitCodeHostRingPc[slot],
                (unsigned)Uae2026JitCodeHostRingPhys[slot],
                (unsigned)Uae2026JitCodeHostRingSr[slot],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][0],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][1],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][2],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][3],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][4],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][5],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][6],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][7],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][8],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][9],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][10],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][11],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][12],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][13],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][14],
                (unsigned)Uae2026JitCodeHostRingRegs[slot][15]);
        for (unsigned wi = 0; wi < 12; wi++)
            fprintf(stderr, " w%u=%04x", wi, (unsigned)Uae2026JitCodeHostRingWords[slot][wi]);
        fprintf(stderr, "\n");
    }
}

static void bridge_trace_fault_words(int prb)
{
    static int initialized = 0;
    static bool enabled = false;
    static bool range_enabled = false;
    static uae_u32 start = 0;
    static uae_u32 end = 0xffffffffu;
    static unsigned long limit = 128;
    static unsigned long count = 0;

    if (!initialized) {
        enabled = env_truthy("B2_JIT_TRACE_FAULT_WORDS", false);
        const char *start_env = getenv("B2_JIT_TRACE_FAULT_WORDS_START");
        const char *end_env = getenv("B2_JIT_TRACE_FAULT_WORDS_END");
        if (start_env && *start_env) {
            start = (uae_u32)strtoul(start_env, nullptr, 0);
            end = (end_env && *end_env) ? (uae_u32)strtoul(end_env, nullptr, 0) : start;
            range_enabled = true;
            enabled = true;
        }
        limit = env_ulong("B2_JIT_TRACE_FAULT_WORDS_LIMIT", 128);
        initialized = 1;
    }
    if (!enabled || count >= limit)
        return;

    const uae_u32 pc = m68k_getpc();
    const uae_u32 fault_pc = regs.fault_pc;
    const uae_u32 fault_addr = regs.mmu_fault_addr;
    if (range_enabled &&
        !bridge_trace_fault_words_in_range(pc, start, end) &&
        !bridge_trace_fault_words_in_range(fault_pc, start, end) &&
        !bridge_trace_fault_words_in_range(fault_addr, start, end))
        return;

    const unsigned long n = count++;
    fprintf(stderr,
            "JIT_FAULT_WORDS n=%lu prb=%d pc=%08x fault_pc=%08x addr=%08x sr=%04x sfc=%u dfc=%u mmu_opcode=%04x mmu_restart=%d pc_p=%p oldp=%p "
            "d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x "
            "a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x a7=%08x\n",
            n, prb, (unsigned)pc, (unsigned)fault_pc, (unsigned)fault_addr,
            (unsigned)regs.sr, (unsigned)regs.sfc, (unsigned)regs.dfc,
            (unsigned)(uae_u16)mmu_opcode, (int)mmu_restart,
            (void *)regs.pc_p, (void *)regs.pc_oldp,
            (unsigned)regs.regs[0], (unsigned)regs.regs[1], (unsigned)regs.regs[2], (unsigned)regs.regs[3],
            (unsigned)regs.regs[4], (unsigned)regs.regs[5], (unsigned)regs.regs[6], (unsigned)regs.regs[7],
            (unsigned)regs.regs[8], (unsigned)regs.regs[9], (unsigned)regs.regs[10], (unsigned)regs.regs[11],
            (unsigned)regs.regs[12], (unsigned)regs.regs[13], (unsigned)regs.regs[14], (unsigned)regs.regs[15]);
    bridge_trace_fault_words_words("LIVE_PC", n, pc);
    if (fault_pc != pc)
        bridge_trace_fault_words_words("LIVE_FPC", n, fault_pc);
    if (fault_addr != pc && fault_addr != fault_pc)
        bridge_trace_fault_words_words("LIVE_ADDR", n, fault_addr);
    if (Uae2026JitLastCodeHostPc) {
        fprintf(stderr, "JIT_FAULT_CODEHOST_LAST n=%lu pc=%08x phys=%08x match=%d",
                n, (unsigned)Uae2026JitLastCodeHostPc, (unsigned)Uae2026JitLastCodeHostPhys,
                (int)(Uae2026JitLastCodeHostPc == pc || Uae2026JitLastCodeHostPc == fault_pc));
        for (unsigned wi = 0; wi < 12; wi++)
            fprintf(stderr, " w%u=%04x", wi, (unsigned)Uae2026JitLastCodeHostWords[wi]);
        fprintf(stderr, "\n");
        bridge_trace_codehost_frame(n);
        bridge_trace_codehost_ring(n);
    }
    if (regs.pc_p) {
        fprintf(stderr, "JIT_FAULT_SHADOW_PCP n=%lu", n);
        for (unsigned wi = 0; wi < 12; wi++)
            fprintf(stderr, " w%u=%04x", wi, (unsigned)bridge_shadow_host_peek_word(regs.pc_p + wi * 2));
        fprintf(stderr, "\n");
    }
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

/* A MOVE's destination store is the last thing the instruction does.  The
 * 68040 therefore reports a fault on that store as a continuation fault: every
 * auto-EA side effect is already committed, the PC has already advanced past
 * the opcode, and the store itself is handed to the kernel in the frame's WB3
 * entry.  The generated 040 interpreter says so literally -- it clears
 * mmufixup, does m68k_incpci(), publishes regs.instruction_pc and sets
 * mmu_restart = false immediately before put_*_mmu040() -- so a JIT fault on
 * the same store must not roll anything back and must not restart.
 *
 * Measured against the interpreter at the same guest point (block-I/O record
 * 53138, MOVE.L -(A0),-(A1) at 0500721c inside libsys _bcopy):
 *   interpreter   pc=0500721e a0=00273ffc a1=11161ffc
 *   JIT (before)  pc=0500721c a0=00273ffc a1=11162000
 * The JIT restarted the instruction having rolled back only the operand whose
 * address matched the fault, so the source predecrement was applied a second
 * time and A0 lost four bytes on every page-crossing fault of a long copy.
 * After three faults the byte tail of that bcopy read below its own source
 * buffer, and the process faulted on an unmapped page for ever.
 *
 * Restricted to EA forms that encode in exactly one word, so the
 * post-instruction PC is fault_pc + 2 with no extension-word decoding. */
static bool bridge_move_dest_write_continuation(uae_u32 pc)
{
    if (!pc || !bridge_code_readable(pc, 2))
        return false;
    const uae_u16 opcode = (uae_u16)bridge_peek_code_word(pc);
    if ((opcode & 0xc000u) != 0 || (opcode & 0x3000u) == 0)
        return false; /* not a MOVE/MOVEA */
    const int src_mode = (opcode >> 3) & 7;
    const int dst_mode = (opcode >> 6) & 7;
    const int dst_reg = (opcode >> 9) & 7;
    /* src Dn/An/(An)/(An)+/-(An) and dst (An)/(An)+/-(An): one opcode word. */
    if (src_mode > 4 || dst_mode < 2 || dst_mode > 4)
        return false;
    if (regs.mmu_ssw & 0x0100u)
        return false; /* SSW RW set: a read fault, not the destination store */
    if (bridge_mmio_addr(regs.mmu_fault_addr))
        return false; /* device cycles are replayed through the interpreter */
    const uae_u32 fault_addr = regs.mmu_fault_addr;
    const uae_u32 areg = m68k_areg(regs, dst_reg);
    if (dst_mode == 3)
        return areg == fault_addr + (uae_u32)bridge_move_size_increment(opcode, dst_reg);
    /* (An) never moves, -(An) has already been decremented to the store
     * address; either way the register must name the faulting bus cycle. */
    return areg == fault_addr;
}

static bool bridge_proven_post_advance_byte_write_opcode(uae_u32 pc)
{
    if (!bridge_code_readable(pc, 2))
        return false;
    const uae_u16 opcode = (uae_u16)bridge_peek_code_word(pc);
    /* Interpreter-oracle covered non-restartable byte-write shapes:
     *   1082  MOVE.B D2,(A0)       (fault_write_byte_d2)
     *   109a  MOVE.B (A2)+,(A0)    (fault_write_byte_postinc)
     * Keep this exact until broader opcode/EA coverage exists. */
    return opcode == 0x1082u || opcode == 0x109au;
}

static bool bridge_normalize_proven_moves_fault_tuple(uae_u32 pc)
{
    if (!bridge_code_readable(pc, 4))
        return false;
    const uae_u16 opcode = (uae_u16)bridge_peek_code_word(pc);
    const uae_u16 ext = (uae_u16)bridge_peek_code_word(pc + 2);

    /* Interpreter-oracle covered MOVES.L shapes:
     *   0e90 0800  MOVES.L D0,(A0) through DFC; non-restartable write reports
     *              the post-extension PC and clears fault_pc/effective EA.
     *   0e90 0000  MOVES.L (A0),D0 through SFC; restartable read reports the
     *              MOVES opcode PC but still clears fault_pc/effective EA. */
    if (opcode == 0x0e90u && !mmu_restart && ext == 0x0800u && regs.mmu_ssw == 0x0401u) {
        const uae_u32 post_pc = pc + 4;
        regs.fault_pc = 0;
        regs.instruction_pc = post_pc;
        regs.mmu_effective_addr = 0;
        m68k_setpc(post_pc);
        return true;
    }
    if (opcode == 0x0e90u && mmu_restart && ext == 0x0000u && regs.mmu_ssw == 0x0501u) {
        regs.fault_pc = 0;
        regs.instruction_pc = pc;
        regs.mmu_effective_addr = 0;
        m68k_setpc(pc);
        return true;
    }

    /* Exact _copyoutmsg forms covered by forced-fault interpreter oracles:
     *   0e19 0800  MOVES.B D0,(A1)+ through DFC
     *   0e99 1800  MOVES.L D1,(A1)+ through DFC
     * For a non-restartable register-to-memory MOVES write, the 68040 commits
     * the postincrement before the bus cycle and reports the post-extension PC.
     * bridge_restore_autoea_fault_side_effects() conservatively rolls native
     * postincrements back first, so reapply only the matching fault-address
     * tuple here.  Keep word and other EA modes excluded until oracle-covered. */
    int postinc = 0;
    unsigned expected_size = 0;
    if ((opcode & 0xfff8u) == 0x0e18u) {
        postinc = areg_byteinc[opcode & 7u];
        expected_size = 0x0020u;
    } else if ((opcode & 0xfff8u) == 0x0e98u) {
        postinc = 4;
        expected_size = 0x0000u;
    }
    const unsigned expected_ssw = 0x0400u | expected_size | (regs.dfc & 0x0007u);
    if (postinc > 0 && !mmu_restart && (ext & 0x0800u) && mmu_opcode == opcode &&
        regs.mmu_ssw == expected_ssw) {
        const int areg = opcode & 7u;
        if (m68k_areg(regs, areg) != regs.mmu_fault_addr)
            return false;
        m68k_areg(regs, areg) = regs.mmu_fault_addr + (uae_u32)postinc;
        const uae_u32 post_pc = pc + 4;
        regs.fault_pc = 0;
        regs.instruction_pc = post_pc;
        regs.mmu_effective_addr = 0;
        m68k_setpc(post_pc);
        return true;
    }
    return false;
}

static bool bridge_normalize_proven_trap_frame_fault_tuple(uae_u32 pc)
{
    if (!bridge_code_readable(pc, 2))
        return false;
    const uae_u16 opcode = (uae_u16)bridge_peek_code_word(pc);
    /* Interpreter-oracle covered nested trap-frame write fault:
     *   4e40  TRAP #0 from user mode.  The SR frame word write faults after the
     *         trap has switched to supervisor and advanced PC past the opcode.
     *         The interpreter pre-Exception dump clears fault_pc/effective EA.
     * Do not generalize to other trap-family opcodes without a matching oracle. */
    if (opcode != 0x4e40u || mmu_restart || regs.mmu_ssw != 0x0445u || mmu_opcode != 0x4e40u)
        return false;
    const uae_u32 post_pc = pc + 2;
    regs.fault_pc = 0;
    regs.instruction_pc = post_pc;
    regs.mmu_effective_addr = 0;
    m68k_setpc(post_pc);
    return true;
}

static bool bridge_normalize_proven_movem_continuation_fault_tuple(uae_u32 pc)
{
    if (!bridge_code_readable(pc, 4))
        return false;
    const uae_u16 opcode = (uae_u16)bridge_peek_code_word(pc);
    const uae_u16 ext = (uae_u16)bridge_peek_code_word(pc + 2);
    /* Interpreter-oracle covered MOVEM.L predecrement continuation fault:
     *   48e0 c000  MOVEM.L D0-D1,-(A0).  The 68040 continuation frame reports
     *              the MOVEM opcode PC, clears fault_pc, and preserves
     *              mmu_effective_addr as the continuation EA (not fault addr).
     * Do not generalize to other MOVEM masks/modes without a matching oracle. */
    if (opcode != 0x48e0u || ext != 0xc000u || !mmu_restart || regs.mmu_ssw != 0x1401u)
        return false;
    regs.fault_pc = 0;
    regs.instruction_pc = pc;
    m68k_setpc(pc);
    return true;
}

extern "C" void Uae2026UspWrite(const char *site, uae_u32 value);

static void bridge_set_active_a7(uae_u32 value)
{
    m68k_areg(regs, 7) = value;
    if (!regs.s)
        Uae2026UspWrite("bridge_a7", value);
    else if (regs.m)
        regs.msp = value;
    else
        regs.isp = value;
}

/* Restore a transaction's pre-instruction A7 after a precise fault.
 *
 * bridge_set_active_a7() mirrors A7 into usp/isp/msp according to the CURRENT
 * regs.s, which is only right while the privilege state has not moved.  RTE
 * pops the exception frame and applies the new SR through MakeFromSR() -- and
 * therefore performs the USP/ISP swap -- BEFORE the return target is fetched,
 * so a fault on that fetch rolls back with regs.s already 0 while pre_a7 is
 * still a supervisor stack pointer.  Mirroring it by the current S wrote a
 * kernel address into regs.usp; the next supervisor->user transition then
 * loaded it into A7 and user code ran on the kernel stack.  Measured:
 *
 *   USPBAD site=bridge_a7 value=11152fa8 s=0 sr=0010 pc=00004364
 *   SPBAD  site=full olds=1 sr=0010 pc=04002162 a7=11152fa8 usp=11152fa8
 *
 * Route the value to the stack it actually belongs to, and leave the active A7
 * alone when the SR change has already selected the other one. */
static bool bridge_restore_txn_a7(uae_u32 value, uae_u32 pre_sr)
{
    const int pre_s = (int)((pre_sr >> 13) & 1);
    const int pre_m = (int)((pre_sr >> 12) & 1);
    if ((int)regs.s == pre_s && (int)regs.m == pre_m) {
        bridge_set_active_a7(value);
        return true;
    }
    /* The privilege state moved between the transaction beginning and the
     * fault: RTE pops the frame and applies the new SR through MakeFromSR(),
     * which performs the USP/ISP swap, BEFORE the return target is fetched.  A
     * fault on that fetch is a NEW exception in the new context, not something
     * to undo -- the interpreter has no rollback here and delivers it as it
     * stands.  Rolling back regardless mirrored a supervisor A7 into regs.usp
     * (measured: USPBAD site=bridge_a7 value=11152fa8 s=0 pc=00004364), and
     * reverting the SR instead resumed the guest at a user PC in supervisor
     * mode.  Decline the rollback. */
    return false;
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
            /* A live call transaction only describes THIS call's target fetch.
             * The runtime helpers (jit_runtime_jsr/bsr) call
             * Uae2026JitMmuTxnCommit() as soon as the push is architecturally
             * done, but natively translated JSR/BSR emit the Begin call and no
             * commit at all, so every completed native call leaves its
             * transaction live.  Without this check the next unrelated MMU
             * fault -- a demand-paged operand anywhere in the callee -- restored
             * that call's pre-push A7, leaving A7 four bytes high; the callee's
             * RTS then popped the caller's first argument as its return address.
             * Measured in libsys bcopy: entered at sp=001ff630, a source page
             * fault in `moveb %a0@-,%a1@-` at 050071fe resumed with sp=001ff634,
             * and the RTS at 05007244 returned to 0024e3dc -- the src argument --
             * which is a data page, so the process took privilege violations at
             * that PC for ever and the window system never came up.
             *
             * The legitimate case is the target code fetch of this very call:
             * the fault PC is the call target and A7 still holds the post-push
             * value. */
            if (!txn.aux1 || fault_pc != txn.aux1 ||
                m68k_areg(regs, 7) != txn.side_new) {
                if (getenv("B2_JIT_TRACE_CALL_ROLLBACK")) {
                    fprintf(stderr,
                            "JIT_CALL_TARGET_ROLLBACK_TXN_STALE fault_pc=%08x op_pc=%08x op=%04x target=%08x addr=%08x sp=%08x oldsp=%08x newsp=%08x\n",
                            (unsigned)fault_pc, (unsigned)txn.pc, (unsigned)txn.opcode,
                            (unsigned)txn.aux1, (unsigned)regs.mmu_fault_addr,
                            (unsigned)m68k_areg(regs, 7),
                            (unsigned)txn.side_old, (unsigned)txn.side_new);
                }
                return false;
            }
            if (!bridge_restore_txn_a7(txn.pre_a7, txn.pre_sr))
                return false;
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
            if (!bridge_restore_txn_a7(txn.pre_a7, txn.pre_sr))
                return false;
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

static void bridge_restore_call_target_fault_side_effects(uae_u32 fault_pc)
{
    const uae_u32 fault_addr = regs.mmu_fault_addr;
    /* Whatever this fault turns out to be, the pending transaction does not
     * survive it: an exception is about to be delivered, and a record kept
     * across it can only be consumed by an unrelated later fault. */
    if (!fault_addr || fault_addr == fault_pc) {
        bridge_clear_mmu_txn();
        return;
    }
    /* A call-target rollback is only valid after the return-address push
     * succeeded and a later target code fetch faulted.  If the faulting address
     * is the just-decremented stack location, this is the call push itself
     * faulting; the 040 interpreter leaves that stack side effect visible for
     * exception delivery instead of undoing it here. */
    if (fault_addr + 4u == m68k_areg(regs, 7)) {
        bridge_clear_mmu_txn();
        return;
    }
    (void)bridge_rollback_mmu_txn(fault_pc);
}

static void bridge_restore_autoea_fault_side_effects(uae_u32 fault_pc, bool restartable)
{
    if (!bridge_code_readable(fault_pc, 2))
        return;
    const uae_u16 opcode = (uae_u16)bridge_peek_code_word(fault_pc);
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
    const uae_u32 opcode = bridge_peek_code_word(pc);
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
    const uae_u32 opcode = bridge_peek_code_word(pc);
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

extern "C" void Uae2026JitBridgeResumeFromHandoff(void)
{
    /* Called by the interpreter dispatch loop after a one-shot RTE-fault
     * handoff window has elapsed; restore the bridge's internal active
     * flag so subsequent fault handling treats JIT as live again.
     * Flush the compiled-block cache first so any blocks compiled
     * pre-handoff are recompiled against the post-fault kernel state. */
    /* Flush the compiled-block cache so any blocks compiled pre-handoff
     * are recompiled against the post-fault kernel state. */
    Uae2026CompilerFlushCacheHard();
    jit_active = true;
    UseJIT = true;
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

extern "C" uintptr_t Uae2026CompilerCacheTagsTable(void);
extern "C" void Uae2026CompilerPrepareMmuDispatch(void);
extern "C" void Uae2026CompilerRefreshDirectBase(void);
extern "C" uintptr_t Uae2026JitMmuXlateCodeHost(uae_u32 addr);

extern "C" void Uae2026JitBridgeCompileExecute(void)
{
    if (!jit_active)
        return;

    /* Since regs is now the shared symbol with JIT fields at correct
     * offsets (via newcpu.h restructure), no register sync is needed.
     * The separate flag structs require conversion of both words. */
    jit_regflags.nzcv = bridge_cznv_legacy_to_jit(regflags.cznv);
    jit_regflags.x = bridge_x_legacy_to_jit(regflags.x);

    /* Native bank-dispatch helpers dereference regs.mem_banks directly.  Keep
     * this stamped at every bridge entry because compiler/bootstrap paths can
     * reset the shared regs fields while rebuilding or invalidating the JIT. */
    regs.mem_banks = env_truthy("PREVIOUS_UAE2026_JIT_RAM", false)
        ? Uae2026JitRamMmuBankTable()
        : (uintptr_t)mem_banks;
    regs.cache_tags = Uae2026CompilerCacheTagsTable();

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

    /* Cache tags are keyed by the translated JIT code-host pointer. Translate
     * before entering pushall; waiting for execute_normal() guarantees a miss
     * and prevents pass-2/native re-entry from witnessing the compiled block. */
    const uae_u32 dispatch_pc = regs.pc & ~1u;
    regs.fault_pc = dispatch_pc;
    Uae2026JitLastInstructionPc = dispatch_pc;
    mmu_restart = true;
    mmu_opcode = 0xffff;
    regs.pc_p = (uae_u8 *)Uae2026JitMmuXlateCodeHost(dispatch_pc);
    regs.pc_oldp = regs.pc_p;
    /* The generated entry stub indexes cache_tags by translated host pointer.
     * Promote only the block matching the full logical/context key; otherwise
     * force execute_normal() to trace/compile that exact alias. */
    Uae2026CompilerPrepareMmuDispatch();

    bool handled_mmu_exception = false;
    int prb = setjmp(__exbuf);
    if (prb != 0) {
        /* Abnormal resume: anything bracketed across the jump has been
         * skipped, including the block verifier's re-entrancy guard. */
        extern void Uae2026JitVerifyNestedRunAborted(void);
        Uae2026JitVerifyNestedRunAborted();
    }
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
        /* Defensive supervisor-stack repair: some native/fallback paths can
         * arrive at the bridge with SR.S set but the active A7 register still
         * zero/stale while ISP holds the real supervisor stack.  If we build a
         * 68040 exception frame with A7=0, the frame push wraps into
         * 0xfffffffc and immediately double-faults. */
        if (regs.s && m68k_areg(regs, 7) == 0 && regs.isp >= 0x1000)
            bridge_set_active_a7(regs.isp);
        if (mmu_restart) {
            regflags.cznv = bridge_cznv_jit_to_legacy(Uae2026JitLastFlags.cznv);
            regflags.x = bridge_x_jit_to_legacy(Uae2026JitLastFlags.x);
            const uae_u32 restart_pc = regs.fault_pc ? regs.fault_pc :
                (Uae2026JitLastInstructionPc ? Uae2026JitLastInstructionPc : regs.instruction_pc);
            m68k_setpc(restart_pc);
        } else if (bridge_helper_state.active &&
            bridge_helper_state.kind == UAE2026_JIT_HELPER_EXACT_OPCODE) {
            /* The separately generated 68040 handler owns non-restartable
             * ordering and has already committed its exact instruction_pc.
             * Drop the direct-address pointer delta installed by the JIT call
             * wrapper; otherwise m68k_getpc() can subtract a non-identity code
             * mapping and turn the committed logical successor into the opcode
             * start (or zero). */
            const uae_u32 post_pc = regs.instruction_pc;
            regs.fault_pc = 0;
            regs.mmu_effective_addr = 0;
            m68k_setpc(post_pc);
        } else if (bridge_helper_state.active &&
            bridge_helper_state.kind == UAE2026_JIT_HELPER_DATA_ACCESS &&
            bridge_helper_state.instruction_bytes != 0) {
            /* Generated 68040 handlers commit their linear PC immediately
             * before non-restartable memory writes (gen_set_fault_pc). Native
             * bank helpers run the same MMU primitive but cannot execute that
             * generated pre-write statement. The producer supplies the exact
             * instruction length, so publish the canonical post-PC tuple here
             * without opcode scanning or an address-specific repair. */
            const uae_u32 post_pc = bridge_helper_state.op_pc +
                bridge_helper_state.instruction_bytes;
            regs.fault_pc = 0;
            regs.instruction_pc = post_pc;
            regs.mmu_effective_addr = 0;
            m68k_setpc(post_pc);
        }
        for (unsigned fixup_index = 0; fixup_index < 2; fixup_index++) {
            if (mmufixup[fixup_index].reg >= 0) {
                m68k_areg(regs, mmufixup[fixup_index].reg) = mmufixup[fixup_index].value;
                mmufixup[fixup_index].reg = -1;
            }
        }
        /* Decided before any rollback, because the rollback is what has to be
         * suppressed: a fault on a MOVE's destination store is a continuation
         * fault and no side effect may be undone. */
        const bool move_dest_write_cont =
            (prb == 2) && bridge_move_dest_write_continuation(regs.fault_pc);
        if (!move_dest_write_cont)
            bridge_restore_autoea_fault_side_effects(regs.fault_pc, mmu_restart);
        /* B2_AUTOEA_TRACE_ADDR=<addr>: report the auto-EA rollback decision for
         * faults at one exact address.  The rollback is keyed on regs.fault_pc
         * and on the fault being restartable; when either is missing the
         * predecrement stays committed and the instruction is no longer
         * restartable, which is invisible from the exception stream alone. */
        {
            static int autoea_trace_state = -1;
            static uae_u32 autoea_trace_addr = 0;
            if (autoea_trace_state < 0) {
                const char *ta = getenv("B2_AUTOEA_TRACE_ADDR");
                autoea_trace_addr = (ta && *ta) ? (uae_u32)strtoul(ta, NULL, 0) : 0;
                autoea_trace_state = autoea_trace_addr ? 0 : 1;
            }
            if (autoea_trace_state == 0 && regs.mmu_fault_addr == autoea_trace_addr) {
                const uae_u32 fpc = regs.fault_pc;
                const bool readable = fpc && bridge_code_readable(fpc, 2);
                fprintf(stderr,
                    "JITAUTOEA prb=%d fault_pc=%08x readable=%d op=%04x ipc=%08x "
                    "restart=%d ssw=%04x fa=%08x a0=%08x a1=%08x a7=%08x\n",
                    prb, (unsigned)fpc, (int)readable,
                    (unsigned)(readable ? (uae_u16)bridge_peek_code_word(fpc) : 0),
                    (unsigned)regs.instruction_pc, (int)mmu_restart,
                    (unsigned)regs.mmu_ssw, (unsigned)regs.mmu_fault_addr,
                    (unsigned)m68k_areg(regs, 0), (unsigned)m68k_areg(regs, 1),
                    (unsigned)m68k_areg(regs, 7));
                fflush(stderr);
            }
        }
        bridge_restore_call_target_fault_side_effects(regs.fault_pc);
        /* Confirmed byte-write seams: the 040 interpreter reports these
         * non-restartable faults after advancing PC to the next instruction.
         * Synthetic forced-fault oracles for the exact MOVE.B D2,(A0) and
         * MOVE.B (A2)+,(A0) shapes also show fault_pc/mmu_effective_addr clear
         * in the pre-Exception dump.  Keep broader/native policy gated to the
         * exact opcodes and preserve the historical PC-only compatibility shim
         * for the original boot seams if an unlisted opcode reaches them. */
        if (move_dest_write_cont) {
            const uae_u32 post_pc = regs.fault_pc + 2;
            mmu_restart = false;
            regs.fault_pc = 0;
            regs.instruction_pc = post_pc;
            regs.mmu_effective_addr = 0;
            m68k_setpc(post_pc);
        } else if (prb == 2 && bridge_normalize_proven_trap_frame_fault_tuple(regs.fault_pc)) {
            /* Exact trap-frame tuple handled above. */
        } else if (prb == 2 && bridge_normalize_proven_movem_continuation_fault_tuple(regs.fault_pc)) {
            /* Exact MOVEM continuation tuple handled above. */
        } else if (prb == 2 && bridge_normalize_proven_moves_fault_tuple(regs.fault_pc)) {
            /* Exact MOVES tuple handled above. */
        } else if (prb == 2 && !mmu_restart && bridge_proven_post_advance_byte_write_opcode(regs.fault_pc)) {
            const uae_u32 post_pc = regs.fault_pc + 2;
            regs.fault_pc = 0;
            regs.instruction_pc = post_pc;
            regs.mmu_effective_addr = 0;
            m68k_setpc(post_pc);
        }
        const bool bridge_rte_fault = bridge_peek_code_word(regs.fault_pc) == 0x4e73u;
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
        if (prb == 2 && !bridge_rte_fault && regs.fault_pc != 0 && regs.fault_pc < 0x00020000u &&
            regs.mmu_fault_addr != regs.fault_pc) {
            const uae_u16 fc = regs.mmu_ssw & 0x0007u; /* 68040 SSW TM/function-code bits */
            /* MMU_SSW_CM: preserve the 68040 MOVEM continuation EA. */
            if (fc != 2 && fc != 6 && !(regs.mmu_ssw & 0x1000u))
                regs.mmu_effective_addr = regs.mmu_fault_addr;
        }
        if (prb == 2 && !bridge_rte_fault && regs.fault_pc >= 0x04000000u && regs.fault_pc < 0x08000000u) {
            /* Previous's legacy format-7 frame builder stores mmu_effective_addr
             * as the EA word.  JIT-delivered helper faults may leave that field
             * stale from an earlier low-virtual fault; make it match the actual
             * bus fault address for bridge-delivered RAM/MMU data cycles.  Do
             * not do this for 68040 continuation frames (MMU_SSW_CM), where the
             * interpreter keeps the continuation EA distinct from the faulting
             * writeback bus address (e.g. MOVEM predecrement). */
            if (!(regs.mmu_ssw & 0x1000u))
                regs.mmu_effective_addr = regs.mmu_fault_addr;
            if (m68k_getpc() != regs.fault_pc)
                m68k_setpc(regs.fault_pc);
        }
        if (prb == 2 && bridge_try_handle_mmio_byte_op())
            return;
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
            bridge_trace_fault_words(prb);
            exc_log_count++;
            if (prb == 2 && regs.fault_pc < 0x00020000u) {
                Uae2026JitLowpcFaultSeq++;
                Uae2026JitLastLowpcFaultPc = regs.fault_pc;
                Uae2026JitLastLowpcFaultAddr = regs.mmu_fault_addr;
                Uae2026JitLastLowpcFaultOpcode = mmu_opcode;
            }
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
        if (Uae2026OpcodeTestModeHandleExpectedException(prb))
            return;
        bridge_trace_lowpc_resume("PRE", prb);
        int prb2 = setjmp(__exbuf);
        if (prb2 != 0) {
            extern void Uae2026JitVerifyNestedRunAborted(void);
            Uae2026JitVerifyNestedRunAborted();
        }
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
                /* Bisection helper: B2_JIT_RTE_FAULT_HANDOFF_SKIP_N defers the
                 * handoff to the Nth RTE fault.  Used to find the latest RTE
                 * fault count where pure-JIT execution still produces
                 * interpreter-equivalent kernel state. */
                static long handoff_skip_n = -1;
                static long handoff_count = 0;
                if (handoff_skip_n < 0) {
                    const char *env = getenv("B2_JIT_RTE_FAULT_HANDOFF_SKIP_N");
                    handoff_skip_n = (env && *env) ? strtol(env, NULL, 0) : 0;
                    if (handoff_skip_n < 0) handoff_skip_n = 0;
                }
                handoff_count++;
                if (handoff_count <= handoff_skip_n) {
                    fprintf(stderr,
                            "UAE2026 bridge: RTE fault handoff DEFERRED count=%ld skip=%ld pc=%08x\n",
                            handoff_count, handoff_skip_n, (unsigned)handled_pc);
                } else {
                    /* Per-event handoff (B2_JIT_RTE_FAULT_HANDOFF_RESUME_INSNS=N):
                     * disable the JIT only for the next N interpreter
                     * instructions, then re-enable it so steady-state work
                     * keeps running native.  Setting N=0 (the default)
                     * preserves the historical permanent handoff used as the
                     * canonical boot recipe. */
                    static long resume_insns = -1;
                    if (resume_insns < 0) {
                        const char *env = getenv("B2_JIT_RTE_FAULT_HANDOFF_RESUME_INSNS");
                        resume_insns = (env && *env) ? strtol(env, NULL, 0) : 0;
                        if (resume_insns < 0) resume_insns = 0;
                    }
                    if (resume_insns > 0) {
                        Uae2026JitInterpResumeCountdown = (unsigned long)resume_insns;
                        fprintf(stderr,
                                "UAE2026 bridge: RTE fault handoff oneshot count=%ld pc=%08x sr=%04x isp=%08x resume_after=%ld\n",
                                handoff_count, (unsigned)handled_pc, (unsigned)regs.sr,
                                (unsigned)regs.isp, resume_insns);
                    } else {
                        fprintf(stderr,
                                "UAE2026 bridge: RTE fault handoff to interpreter count=%ld pc=%08x sr=%04x isp=%08x\n",
                                handoff_count, (unsigned)handled_pc, (unsigned)regs.sr,
                                (unsigned)regs.isp);
                    }
                    UseJIT = false;
                    jit_active = false;
                }
            }
        } else {
            __exvalue = prb2;
            if (__is_catched())
                __poptry();
            fprintf(stderr, "UAE2026 bridge: fatal double MMU exception %d while handling %d\n", prb2, prb);
            prev_m68k_reset(1);
        }
    }

    /* A helper fault longjmps past its normal commit/clear path.  Exception
     * handling has consumed the saved restart phase; do not let the next
     * helper inherit an active transaction. */
    Uae2026JitHelperClear();

    /* Sync flag struct back.  If the bridge handled an MMU exception,
     * Exception() and the interpreter-side path have already updated
     * regflags; do not overwrite them with stale JIT-entry flags. */
    if (handled_mmu_exception) {
        jit_regflags.nzcv = bridge_cznv_legacy_to_jit(regflags.cznv);
        jit_regflags.x = bridge_x_legacy_to_jit(regflags.x);
    } else {
        regflags.cznv = bridge_cznv_jit_to_legacy(jit_regflags.nzcv);
        regflags.x = bridge_x_jit_to_legacy(jit_regflags.x);
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
    const char *opcode_test_hex = getenv("B2_TEST_HEX");
    const bool opcode_test_requested = opcode_test_hex && *opcode_test_hex;
    const bool translated_mode_requested =
        env_truthy("PREVIOUS_UAE2026_JIT_RAM", false) || opcode_test_requested;
    if (!prefs.requested || !translated_mode_requested) {
        UseJIT = false;
        jit_active = false;
        fprintf(stderr,
                "UAE2026 bridge: %s; translated_mode=%s; regstruct=%zu bytes; cpu_compatible=%s; fpu_strict=%s; special_mem_default=%d\n",
                update_bridge_summary(), bool_word(translated_mode_requested), sizeof(regstruct),
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
    regs.cache_tags = Uae2026CompilerCacheTagsTable();

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
                /* compiler_init() runs before the sparse shadow exists, so its
                 * direct-jump bank table still contains a zero base. Refresh it
                 * now, before any generated JSR/JMP can materialize PC_P. */
                Uae2026CompilerRefreshDirectBase();
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
