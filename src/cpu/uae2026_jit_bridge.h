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
    UAE2026_JIT_HELPER_RETURN = 3,
    UAE2026_JIT_HELPER_DATA_ACCESS = 4,
    UAE2026_JIT_HELPER_EXACT_OPCODE = 5
};

enum Uae2026JitHelperFlagAuthority {
    UAE2026_JIT_FLAGS_ARE_JIT = 0,
    UAE2026_JIT_FLAGS_ARE_PREVIOUS = 1
};

/* Descriptor layout: opcode[15:0], kind[23:16], optional linear instruction
 * length[31:24].  Existing semantic helpers leave the length zero; native bank
 * helpers publish it so a non-restartable 68040 write fault can commit the
 * canonical post-instruction PC without reconstructing it in the bridge. */
#define UAE2026_JIT_HELPER_DESCRIPTOR(opcode, kind) \
    ((((uint32_t)(kind) & 0xffu) << 16) | ((uint32_t)(opcode) & 0xffffu))
#define UAE2026_JIT_HELPER_ACCESS_DESCRIPTOR(opcode, instruction_bytes) \
    ((((uint32_t)(instruction_bytes) & 0xffu) << 24) | \
     UAE2026_JIT_HELPER_DESCRIPTOR((opcode), UAE2026_JIT_HELPER_DATA_ACCESS))

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
void Uae2026JitFallbackCensus(uint32_t opcode, uint32_t site);
void Uae2026JitHelperCommitLogicalPc(uint32_t logical_pc, uint32_t flag_authority);
void Uae2026JitHelperCommitCurrentPc(void);
void Uae2026JitHelperCommitArchitecturalPc(void);
void Uae2026JitHelperClear(void);
void Uae2026JitPrepareContinuationWrite(uint32_t post_pc);
uintptr_t Uae2026JitPrepareMmuDispatchTarget(uint32_t logical_pc);
uint32_t Uae2026JitMmuGeneration(void);
void Uae2026JitMmuTranslationChanged(uint32_t source);
void Uae2026JitMmuTxnBeginCallPushTarget(uint32_t pc, uint32_t target_pc);
void Uae2026JitMmuTxnBeginCallPushCurrentA7ForOpcode(uint32_t pc, uint32_t opcode);
void Uae2026JitMmuTxnBeginReturnPopCurrentA7ByOpcode(uint32_t pc, uint32_t opcode);
void Uae2026JitMmuTxnCommit(void);
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
static inline void Uae2026JitFallbackCensus(uint32_t opcode, uint32_t site) {
    (void)opcode; (void)site;
}
static inline void Uae2026JitHelperCommitLogicalPc(uint32_t logical_pc, uint32_t flag_authority) {
    (void)logical_pc; (void)flag_authority;
}
static inline void Uae2026JitHelperCommitCurrentPc(void) {}
static inline void Uae2026JitHelperCommitArchitecturalPc(void) {}
static inline void Uae2026JitHelperClear(void) {}
static inline void Uae2026JitPrepareContinuationWrite(uint32_t post_pc) { (void)post_pc; }
static inline uintptr_t Uae2026JitPrepareMmuDispatchTarget(uint32_t logical_pc) {
    (void)logical_pc; return 0;
}
static inline uint32_t Uae2026JitMmuGeneration(void) { return 0; }
static inline void Uae2026JitMmuTranslationChanged(uint32_t source) { (void)source; }
static inline void Uae2026JitMmuTxnBeginCallPushTarget(uint32_t pc, uint32_t target_pc) {
    (void)pc; (void)target_pc;
}
static inline void Uae2026JitMmuTxnBeginCallPushCurrentA7ForOpcode(uint32_t pc, uint32_t opcode) {
    (void)pc; (void)opcode;
}
static inline void Uae2026JitMmuTxnBeginReturnPopCurrentA7ByOpcode(uint32_t pc, uint32_t opcode) {
    (void)pc; (void)opcode;
}
static inline void Uae2026JitMmuTxnCommit(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif
