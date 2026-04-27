#ifndef PREVIOUS_UAE2026_COMPILER_PREFS_SHIM_H
#define PREVIOUS_UAE2026_COMPILER_PREFS_SHIM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ENABLE_EXPERIMENTAL_UAE2026_JIT)
bool Uae2026CompilerPrefsShimAvailable(void);
bool Uae2026CompilerPrefsSync(bool checkonly);
int Uae2026CompilerPrefsCacheSizeKB(void);
bool Uae2026CompilerPrefsFPUEnabled(void);
bool Uae2026CompilerPrefsConstJumpEnabled(void);
bool Uae2026CompilerPrefsHardFlushEnabled(void);
int Uae2026CompilerPrefsSpecialMemDefault(void);
#else
static inline bool Uae2026CompilerPrefsShimAvailable(void) { return false; }
static inline bool Uae2026CompilerPrefsSync(bool checkonly) { (void)checkonly; return false; }
static inline int Uae2026CompilerPrefsCacheSizeKB(void) { return 0; }
static inline bool Uae2026CompilerPrefsFPUEnabled(void) { return false; }
static inline bool Uae2026CompilerPrefsConstJumpEnabled(void) { return false; }
static inline bool Uae2026CompilerPrefsHardFlushEnabled(void) { return false; }
static inline int Uae2026CompilerPrefsSpecialMemDefault(void) { return 0; }
#endif

#ifdef __cplusplus
}
#endif

#endif
