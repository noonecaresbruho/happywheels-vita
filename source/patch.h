#ifndef HAPPYWHEELS_PATCH_H
#define HAPPYWHEELS_PATCH_H

#include <stdint.h>

// Feeds a Vita button (AKEYCODE_*) into the in-game driving controls.
void gameplay_key(int32_t keycode, int pressed);

// Left stick: up/down = accelerate/brake, left/right = lean.
void gameplay_stick(float x, float y);

#endif // HAPPYWHEELS_PATCH_H
