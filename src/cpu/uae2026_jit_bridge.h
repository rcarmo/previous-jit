#ifndef PREVIOUS_UAE2026_JIT_BRIDGE_H
#define PREVIOUS_UAE2026_JIT_BRIDGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ENABLE_EXPERIMENTAL_UAE2026_JIT)
bool Uae2026JitBridgeCompiled(void);
bool Uae2026JitBridgeRequested(void);
bool Uae2026JitBridgeBootstrapReady(void);
bool Uae2026JitBridgeBootstrapAttempted(void);
bool Uae2026JitBridgeBootstrapActive(void);
bool Uae2026JitBridgeIsActive(void);   /* JIT dispatch fully live */
void Uae2026JitBridgeCompileExecute(void); /* main JIT loop step */
const char *Uae2026JitBridgeSummary(void);
void Uae2026JitBridgeInit(void);
void Uae2026JitBridgeShutdown(void);
#else
static inline bool Uae2026JitBridgeCompiled(void) { return false; }
static inline bool Uae2026JitBridgeRequested(void) { return false; }
static inline bool Uae2026JitBridgeBootstrapReady(void) { return false; }
static inline bool Uae2026JitBridgeBootstrapAttempted(void) { return false; }
static inline bool Uae2026JitBridgeBootstrapActive(void) { return false; }
static inline bool Uae2026JitBridgeIsActive(void) { return false; }
static inline void Uae2026JitBridgeCompileExecute(void) {}
static inline const char *Uae2026JitBridgeSummary(void) {
    return "uae2026-jit bridge not compiled";
}
static inline void Uae2026JitBridgeInit(void) {}
static inline void Uae2026JitBridgeShutdown(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif
