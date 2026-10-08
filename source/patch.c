/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>
#include <stdio.h>
#include <string.h>
#include <vitasdk.h>

#ifdef __cplusplus
extern "C"
{
#endif
	extern so_module so_mod;
#ifdef __cplusplus
};
#endif

#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX (0x0C20D050)

#include "utils/logger.h"
#include "utils/dialog.h"
#include "reimpl/sys.h"
#include <stdbool.h>

static void controls_patch(void);

void __kuser_memory_barrier(void) {
	__sync_synchronize();
}

void kuser_patch(void) {
	SceKernelAllocMemBlockKernelOpt opt;
	memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
	opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
	opt.attr = 0x1;
	opt.field_C = (SceUInt32)0x9A000000;
	if (kuKernelAllocMemBlock("atomic", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, 0x1000, &opt) < 0)
		fatal_error("Error could not allocate atomic block.");
	kuKernelMemProtect((void *)0x9A000000, (SceSize)0x1000, KU_KERNEL_PROT_EXEC | KU_KERNEL_PROT_READ | KU_KERNEL_PROT_WRITE);

	hook_addr(0x9A000FA0, (uintptr_t)__kuser_memory_barrier);
	hook_addr(0x9A000FC0, (uintptr_t)__atomic_cmpxchg);

	uint32_t patched_addr;
	for (uint32_t addr = so_mod.text_base; addr < so_mod.text_base + so_mod.text_size; addr += 4) {
		uint32_t *a = (uint32_t *)addr;
		if (*a == 0xFFFF0FC0) {
			l_debug("Patching 0x%x -> __kuser_cmpxchg", a);
			patched_addr = 0x9A000FC0;
			kuKernelCpuUnrestrictedMemcpy((void *)(addr), &patched_addr, sizeof(uint32_t));
		}
		else if (*a == 0xFFFF0FA0) {
			l_debug("Patching 0x%x -> __kuser_memory_barrier", a);
			patched_addr = 0x9A000FA0;
			kuKernelCpuUnrestrictedMemcpy((void *)(addr), &patched_addr, sizeof(uint32_t));
		}
	}
}

void so_patch(void) {
	kuser_patch();
	controls_patch();
	// Sample hook with symbol name
	// hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN6glitch2os7Printer5printEPKcz"), (uintptr_t)&hookedFunction);
	// Or with offset
	// hook_addr((uintptr_t)so_mod.text_base + 0xdeadbabe, (uintptr_t)&hookedFunction);
	// If you use SO_CONTINUE, define a so_hook before the function and assign to it
	// function_hook = hook_addr(...);
}
/*
 * Gameplay controls
 *
 * GameplayControls::getState() returns the sum of getStateValue() of every
 * on-screen button that is currently touched (one bit per button). We hook
 * it and OR in the bits of the Vita buttons that are held.
 */

#include "reimpl/controls.h"

typedef struct { float x, y; } Vec2;

#define CONTROLS_BTNS_BEGIN 0x2c4 // std::vector<GameplayBtn*> in GameplayControls
#define CONTROLS_BTNS_END   0x2c8
#define BTN_STATE_VALUE     0x408 // GameplayBtn::stateValue

#define STATE_FORWARD       0x01
#define STATE_BACKWARD      0x02
#define STATE_LEAN_FORWARD  0x04
#define STATE_LEAN_BACKWARD 0x08
#define STATE_SPECIAL       0x10
#define STATE_EJECT         0x80

static so_hook getState_hook;
static volatile uint8_t key_state = 0;
static volatile uint8_t stick_state = 0;

static const Vec2 *(*Node_getPosition)(void *node);
static void (*getForwardPos)(Vec2 *out, void *controls);
static void (*getBackwardPos)(Vec2 *out, void *controls);
static void (*getLeanForwardPos)(Vec2 *out, void *controls);
static void (*getLeanBackwardPos)(Vec2 *out, void *controls);
static void (*getSpecialPos)(Vec2 *out, void *controls);
static void (*getEjectPos)(Vec2 *out, void *controls);

static void dump_controls(void *controls) {
    void **it = *(void ***)((uintptr_t)controls + CONTROLS_BTNS_BEGIN);
    void **end = *(void ***)((uintptr_t)controls + CONTROLS_BTNS_END);
    for (; it != end; it++) {
        const Vec2 *p = Node_getPosition(*it);
        l_info("[controls] btn %p value 0x%x pos %.1f,%.1f", *it,
               *(uint32_t *)((uintptr_t)*it + BTN_STATE_VALUE), p->x, p->y);
    }
    Vec2 v;
    getForwardPos(&v, controls);      l_info("[controls] forward %.1f,%.1f", v.x, v.y);
    getBackwardPos(&v, controls);     l_info("[controls] backward %.1f,%.1f", v.x, v.y);
    getLeanForwardPos(&v, controls);  l_info("[controls] leanForward %.1f,%.1f", v.x, v.y);
    getLeanBackwardPos(&v, controls); l_info("[controls] leanBackward %.1f,%.1f", v.x, v.y);
    getSpecialPos(&v, controls);      l_info("[controls] special %.1f,%.1f", v.x, v.y);
    getEjectPos(&v, controls);        l_info("[controls] eject %.1f,%.1f", v.x, v.y);
}

uint8_t GameplayControls_getState(void *this) {
#ifdef DEBUG_SOLOADER
    static void *last = NULL;
    if (this != last) {
        last = this;
        dump_controls(this);
    }
#endif
    uint8_t state = SO_CONTINUE(uint8_t, getState_hook, this);
#ifdef DEBUG_SOLOADER
    static int calls = 0;
    if (++calls % 60 == 0)
        l_info("[controls] getState calls %i, touch 0x%x keys 0x%x stick 0x%x", calls, state, key_state, stick_state);
#endif
    return state | key_state | stick_state;
}

static void controls_patch(void) {
    Node_getPosition = (void *)so_symbol(&so_mod, "_ZNK7cocos2d4Node11getPositionEv");
    getForwardPos = (void *)so_symbol(&so_mod, "_ZN16GameplayControls13getForwardPosEv");
    getBackwardPos = (void *)so_symbol(&so_mod, "_ZN16GameplayControls14getBackwardPosEv");
    getLeanForwardPos = (void *)so_symbol(&so_mod, "_ZN16GameplayControls17getLeanForwardPosEv");
    getLeanBackwardPos = (void *)so_symbol(&so_mod, "_ZN16GameplayControls18getLeanBackwardPosEv");
    getSpecialPos = (void *)so_symbol(&so_mod, "_ZN16GameplayControls13getSpecialPosEv");
    getEjectPos = (void *)so_symbol(&so_mod, "_ZN16GameplayControls11getEjectPosEv");
    getState_hook = hook_addr(so_symbol(&so_mod, "_ZN16GameplayControls8getStateEv"),
                              (uintptr_t)&GameplayControls_getState);
}

static uint8_t key_to_state(int32_t keycode) {
    switch (keycode) {
        case AKEYCODE_DPAD_UP:     return STATE_FORWARD;
        case AKEYCODE_BUTTON_R1:   return STATE_FORWARD;
        case AKEYCODE_DPAD_DOWN:   return STATE_BACKWARD;
        case AKEYCODE_BUTTON_L1:   return STATE_BACKWARD;
        case AKEYCODE_DPAD_RIGHT:  return STATE_LEAN_FORWARD;
        case AKEYCODE_DPAD_LEFT:   return STATE_LEAN_BACKWARD;
        case AKEYCODE_BUTTON_A:    return STATE_SPECIAL;   // Cross
        case AKEYCODE_BUTTON_X:    return STATE_SPECIAL;   // Square
        case AKEYCODE_BUTTON_Y:    return STATE_EJECT;     // Triangle
        default:                   return 0;
    }
}

void gameplay_key(int32_t keycode, int pressed) {
    uint8_t bit = key_to_state(keycode);
    if (pressed)
        key_state |= bit;
    else
        key_state &= ~bit;
}

void gameplay_stick(float x, float y) {
    const float threshold = 0.4f;
    uint8_t s = 0;
    if (y < -threshold) s |= STATE_FORWARD;
    if (y > threshold)  s |= STATE_BACKWARD;
    if (x > threshold)  s |= STATE_LEAN_FORWARD;
    if (x < -threshold) s |= STATE_LEAN_BACKWARD;
    stick_state = s;
}
