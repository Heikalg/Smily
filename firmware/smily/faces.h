// ---------------------------------------------------------------------------
// Faces - pure renderers. Given (animation, frame index, context) each one
// composes a complete 128x64 off-screen buffer. No timing, no state, no
// flushing: the Animation Engine owns all of that.
//
// Style: Dasai-Mochi-ish. A soft white blob with tiny stubby arms and feet,
// a cut-out face placed low on the body, and no text anywhere (which also
// means no font has to be linked into flash).
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

#include "animation.h"

void faces_render(AnimId id, uint16_t frame, const FaceCtx &ctx);

// Integer trig, shared with anything that needs a cheap wobble.
// angle is 0..255 for a full turn, result is -127..127.
int8_t isin(uint8_t angle);
int8_t icos(uint8_t angle);
