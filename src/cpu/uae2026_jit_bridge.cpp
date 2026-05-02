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

/* UseJIT flag (defined in uae2026_linker_stubs.cpp) */
extern bool UseJIT;

/* MEMBaseDiff -- host base offset for the JIT's direct-addressing  */
extern uintptr_t jit_MEMBaseDiff;
extern "C" void Uae2026JitSyncRamToShadow(void);

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
        extern uae_u8 NEXTRam[];
        if (jit_MEMBaseDiff) {
            const size_t ram_size = 64UL * 1024 * 1024;
            memcpy((void *)(jit_MEMBaseDiff + 0x04000000), NEXTRam, ram_size);
        }
    }

    /* Ensure pc_p is NULL so execute_normal() always re-derives from
     * regs.pc (which m68k_reset sets correctly) on every dispatch.     */
    regs.pc_p    = nullptr;
    regs.pc_oldp = nullptr;

    m68k_do_compile_execute();

    /* Sync flag struct back */
    regflags.cznv = jit_regflags.nzcv;
    regflags.x    = jit_regflags.x;

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
     *  0. Set currprefs.cachesize — the JIT reads this to allocate the cache.
     *                        Previous's currprefs doesn't normally have this
     *                        set (Hatari doesn't use JIT cache size).
     *  1. init_table68k()  — build the 68k opcode table (jit_table68k[]).
     *                        BridgeInit() is called BEFORE init_m68k() in
     *                        hatari-glue.c so we must do this ourselves.
     *  2. build_comp()     — builds opcode property tables, allocates the
     *                        JIT code cache (create_popalls + alloc_cache),
     *                        writes the popall stubs (compemu_reset), and
     *                        sets cache_enabled = 1. All-in-one.
     */
    /* Inject cache size so alloc_cache() doesn't bail with cachesize==0 */
    const int jit_cache_kb = Uae2026CompilerPrefsCacheSizeKB();
    if (jit_cache_kb > 0)
        currprefs.cachesize = jit_cache_kb;
    else
        currprefs.cachesize = 8192;  /* 8 MB default */
    changed_prefs.cachesize = currprefs.cachesize;

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
        compiler_initialized = false;
        goto bri_init_done;
    }

    /* Point the JIT's bank-dispatch table at Previous's addrbank array.
     * mem_banks is addrbank*[65536] with SAVE_MEMORY_BANKS.             */
    regs.mem_banks = (uintptr_t)mem_banks;

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
        const uintptr_t shadow_size = 0x10040000UL; /* covers ROM, RAM, and 0x0b-0x0f VRAM windows */

        uae_u8 *shadow = (uae_u8 *)mmap(
            NULL, shadow_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (shadow == MAP_FAILED) {
            fprintf(stderr, "UAE2026 bridge: shadow mmap failed (%s)\n", strerror(errno));
            /* Fall back — jit_MEMBaseDiff stays 0, JIT will crash on ROM access */
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

            jit_MEMBaseDiff = (uintptr_t)shadow;
            /* Make the shadow region executable so popall-derived JIT blocks
             * that land in the shadow don't fault. */
            mprotect(shadow, shadow_size, PROT_READ | PROT_WRITE | PROT_EXEC);
        }

        /* Clear pc_p/pc_oldp so execute_normal() re-derives from regs.pc.    *
         * Also set the compiler unit's RAMBaseHost so the sanity guard fires.  */
        regs.pc_p    = nullptr;
        regs.pc_oldp = nullptr;
        extern uae_u8 *RAMBaseHost;
        RAMBaseHost = shadow + 0x04000000;   /* guard: pcp=0 < shadow+RAM */
        extern uae_u32 RAMSize;
        RAMSize = 64 * 1024 * 1024;
        extern uae_u8 *ROMBaseHost;
        ROMBaseHost = shadow + 0x01000000;
        extern uae_u32 ROMSize;
        ROMSize = 0x20000;
        extern uae_u32 ROMBaseMac;
        ROMBaseMac = 0x01000000;
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
    if (jit_active) {
        UseJIT = false;
        jit_active = false;
    }
    if (compiler_initialized) {
        compiler_exit();
        compiler_initialized = false;
    }
    if (!bootstrap_cache)
        return;

#if defined(__linux__) || defined(__APPLE__)
    munmap(bootstrap_cache, bootstrap_cache_bytes);
#else
    free(bootstrap_cache);
#endif
    bootstrap_cache = nullptr;
    bootstrap_cache_bytes = 0;
    bootstrap_active = false;
}

#endif
