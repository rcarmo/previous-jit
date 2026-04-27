#ifndef PREVIOUS_UAE2026_OPCODE_TEST_H
#define PREVIOUS_UAE2026_OPCODE_TEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Uae2026OpcodeTestModeSetup(void);
bool Uae2026OpcodeTestModeActive(void);
void Uae2026OpcodeTestModeFinish(void);

#ifdef __cplusplus
}
#endif

#endif
