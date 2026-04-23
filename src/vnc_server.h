#ifndef PREVIOUS_VNC_SERVER_H
#define PREVIOUS_VNC_SERVER_H

#include <stdbool.h>
#include <stdint.h>

bool VNCServerEnabled(void);
void VNCServerInitFromEnv(int width, int height);
void VNCServerShutdown(void);
void VNCServerUpdateRGBA(const uint8_t *pixels, int pitch, int width, int height);

#endif
