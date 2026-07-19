#ifndef PREVIOUS_UAE2026_JIT_BRIDGE_H
#define PREVIOUS_UAE2026_JIT_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

/* Runtime semantic helpers cross from the imported direct-address JIT into
 * Previous's logical-PC/MMU state model.  The packed descriptor keeps the
 * generated AArch64 call boundary at two arguments. */
enum Uae2026JitHelperKind {
    UAE2026_JIT_HELPER_GENERIC = 0,
    UAE2026_JIT_HELPER_RTE = 1,
    UAE2026_JIT_HELPER_CALL = 2,
    UAE2026_JIT_HELPER_RETURN = 3
};

enum Uae2026JitHelperFlagAuthority {
    UAE2026_JIT_FLAGS_ARE_JIT = 0,
    UAE2026_JIT_FLAGS_ARE_PREVIOUS = 1
};

#define UAE2026_JIT_HELPER_DESCRIPTOR(opcode, kind) \
    ((((uint32_t)(kind) & 0xffffu) << 16) | ((uint32_t)(opcode) & 0xffffu))

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
void Uae2026JitBridgeRequestBlockExit(unsigned int source);
void Uae2026JitBridgeCompileExecute(void); /* main JIT loop step */
const char *Uae2026JitBridgeSummary(void);
void Uae2026JitBridgeInit(void);
void Uae2026JitBridgeSyncOpcodeTestShadow(void);
void Uae2026JitBridgeShutdown(void);
void Uae2026JitHelperBegin(uint32_t op_pc, uint32_t descriptor);
void Uae2026JitHelperCommitLogicalPc(uint32_t logical_pc, uint32_t flag_authority);
void Uae2026JitHelperClear(void);
#else
static inline bool Uae2026JitBridgeCompiled(void) { return false; }
static inline bool Uae2026JitBridgeRequested(void) { return false; }
static inline bool Uae2026JitBridgeBootstrapReady(void) { return false; }
static inline bool Uae2026JitBridgeBootstrapAttempted(void) { return false; }
static inline bool Uae2026JitBridgeBootstrapActive(void) { return false; }
static inline bool Uae2026JitBridgeIsActive(void) { return false; }
static inline void Uae2026JitBridgeRequestBlockExit(unsigned int source) { (void)source; }
static inline void Uae2026JitBridgeCompileExecute(void) {}
static inline const char *Uae2026JitBridgeSummary(void) {
    return "uae2026-jit bridge not compiled";
}
static inline void Uae2026JitBridgeInit(void) {}
static inline void Uae2026JitBridgeSyncOpcodeTestShadow(void) {}
static inline void Uae2026JitBridgeShutdown(void) {}
static inline void Uae2026JitHelperBegin(uint32_t op_pc, uint32_t descriptor) {
    (void)op_pc; (void)descriptor;
}
static inline void Uae2026JitHelperCommitLogicalPc(uint32_t logical_pc, uint32_t flag_authority) {
    (void)logical_pc; (void)flag_authority;
}
static inline void Uae2026JitHelperClear(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif
