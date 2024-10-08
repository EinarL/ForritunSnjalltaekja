#ifndef GUARD_APP_PING_H
#define GUARD_APP_PING_H

#include <stdint.h>

#include "lownet.h"

void ping(uint8_t node);

void ping_receive(const lownet_frame_t* frame);

#endif
