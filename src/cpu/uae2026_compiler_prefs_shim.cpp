#include "uae2026_compiler_prefs_shim.h"

#if defined(ENABLE_EXPERIMENTAL_UAE2026_JIT)

#include "options_cpu.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace previous_uae2026_compiler {

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

static int current_cpu_pref(void)
{
    switch (::currprefs.cpu_model) {
    case 68000: return 0;
    case 68010: return 1;
    case 68020: return 2;
    case 68030: return 3;
    case 68040: return 4;
    default: return ::currprefs.cpu_level >= 4 ? 4 : 3;
    }
}

bool PrefsFindBool(const char *name)
{
    if (!name)
        return false;
    if (!strcmp(name, "jit"))
        return env_truthy("PREVIOUS_UAE2026_JIT", false);
    if (!strcmp(name, "jitfpu"))
        return env_truthy("PREVIOUS_UAE2026_JIT_FPU", false);
    if (!strcmp(name, "jitlazyflush"))
        return env_truthy("PREVIOUS_UAE2026_JIT_LAZY_FLUSH", true);
    if (!strcmp(name, "jitinline"))
        return env_truthy("PREVIOUS_UAE2026_JIT_CONST_JUMP", true);
    return false;
}

int32_t PrefsFindInt32(const char *name)
{
    if (!name)
        return 0;
    if (!strcmp(name, "jitcachesize"))
        return env_int("PREVIOUS_UAE2026_JIT_CACHE_KB", 8192);
    if (!strcmp(name, "cpu"))
        return current_cpu_pref();
    return 0;
}

int special_mem_default = 0;

#include "uae_cpu_2026/compemu_prefs.cpp"

} // namespace previous_uae2026_compiler

extern "C" bool Uae2026CompilerPrefsShimAvailable(void)
{
    return true;
}

extern "C" bool Uae2026CompilerPrefsSync(bool checkonly)
{
    return previous_uae2026_compiler::check_prefs_changed_comp(checkonly);
}

extern "C" int Uae2026CompilerPrefsCacheSizeKB(void)
{
    return previous_uae2026_compiler::currprefs.cachesize;
}

extern "C" bool Uae2026CompilerPrefsFPUEnabled(void)
{
    return previous_uae2026_compiler::currprefs.compfpu;
}

extern "C" bool Uae2026CompilerPrefsConstJumpEnabled(void)
{
    return previous_uae2026_compiler::currprefs.comp_constjump != 0;
}

extern "C" bool Uae2026CompilerPrefsHardFlushEnabled(void)
{
    return previous_uae2026_compiler::currprefs.comp_hardflush;
}

extern "C" int Uae2026CompilerPrefsSpecialMemDefault(void)
{
    return previous_uae2026_compiler::special_mem_default;
}

#endif
