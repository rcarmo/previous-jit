#ifndef PREVIOUS_UAE2026_OPCODE_TEST_H
#define PREVIOUS_UAE2026_OPCODE_TEST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Uae2026OpcodeTestModeSetup(void);
bool Uae2026OpcodeTestModeActive(void);
bool Uae2026OpcodeTestModeHandleStopTrailerAt(uint32_t logical_pc);
bool Uae2026OpcodeTestModeHandleStopTrailer(void);
bool Uae2026OpcodeTestModeHandleExpectedException(int vector);
bool Uae2026OpcodeTestShouldFaultCode(uint32_t addr, int size);
bool Uae2026OpcodeTestShouldFaultData(uint32_t addr, int size, bool write);
void Uae2026OpcodeTestModeFinish(void);

#ifdef __cplusplus
}
#endif

#endif
