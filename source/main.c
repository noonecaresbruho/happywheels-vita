#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/dialog.h"

#include <psp2/io/stat.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "reimpl/controls.h"
#include "java.h"
#include "patch.h"

int _newlib_heap_size_user = 192 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;

#define SCREEN_W 960
#define SCREEN_H 544

// Fake Java object handles. FalsoJNI never dereferences them.
#define FAKE_CONTEXT       ((jobject)0x42424242)
#define FAKE_ASSET_MANAGER ((jobject)0x41414141)

static void (*nativeSetContext)(JNIEnv *, jclass, jobject context, jobject assetManager);
static void (*sdkboxNativeInit)(JNIEnv *, jclass, jobject context, jobject unk);
static void (*nativeSetAudioDeviceInfo)(JNIEnv *, jclass, jboolean lowLatency, jint sampleRate, jint bufferSize);
static jintArray (*getGLContextAttrs)(JNIEnv *, jclass);
static void (*nativeInit)(JNIEnv *, jclass, jint w, jint h);
static void (*nativeRender)(JNIEnv *, jclass);
static void (*nativeOnPause)(JNIEnv *, jclass);
static void (*nativeOnResume)(JNIEnv *, jclass);
static void (*nativeTouchesBegin)(JNIEnv *, jclass, jint id, jfloat x, jfloat y);
static void (*nativeTouchesEnd)(JNIEnv *, jclass, jint id, jfloat x, jfloat y);
static void (*nativeTouchesMove)(JNIEnv *, jclass, jintArray ids, jfloatArray xs, jfloatArray ys);
static jboolean (*nativeKeyEvent)(JNIEnv *, jclass, jint keyCode, jboolean isPressed);

static void *sym(const char *name) {
    void *f = (void *)so_symbol(&so_mod, name);
    if (!f) {
        l_fatal("Missing symbol %s", name);
        fatal_error("Error: %s not found in libMyGame.so.", name);
    }
    return f;
}

int main() {
    soloader_init_all();
    prefs_load();
    sceIoMkdir(DATA_PATH "files", 0777);

    nativeSetContext = sym("Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetContext");
    sdkboxNativeInit = sym("Java_com_sdkbox_plugin_SDKBox_nativeInit");
    nativeSetAudioDeviceInfo = sym("Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetAudioDeviceInfo");
    getGLContextAttrs = sym("Java_org_cocos2dx_lib_Cocos2dxActivity_getGLContextAttrs");
    nativeInit = sym("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
    nativeRender = sym("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");
    nativeOnPause = sym("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause");
    nativeOnResume = sym("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume");
    nativeTouchesBegin = sym("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin");
    nativeTouchesEnd = sym("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd");
    nativeTouchesMove = sym("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove");
    nativeKeyEvent = sym("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyEvent");

    int (*JNI_OnLoad)(void *jvm) = sym("JNI_OnLoad");
    JNI_OnLoad(&jvm);
    l_success("JNI_OnLoad done.");

    // Same order as Cocos2dxActivity.onCreate()
    nativeSetContext(&jni, NULL, FAKE_CONTEXT, FAKE_ASSET_MANAGER);
    // SDKBox (IAP/analytics plugin) caches the JavaVM here; without it the
    // game calls through a NULL JavaVM as soon as the store initializes.
    sdkboxNativeInit(&jni, NULL, FAKE_CONTEXT, NULL);
    nativeSetAudioDeviceInfo(&jni, NULL, JNI_TRUE, 48000, 512);
    getGLContextAttrs(&jni, NULL);
    l_success("Context set.");

    gl_init();
    nativeInit(&jni, NULL, SCREEN_W, SCREEN_H);
    l_success("Cocos2d-x initialized.");

    int frame = 0;
    while (1) {
        controls_poll();
        nativeRender(&jni, NULL);
        gl_swap();

        // Persist UserDefault changes every ~2 seconds
        if (++frame % 120 == 0)
            prefs_flush();
    }

    sceKernelExitDeleteThread(0);
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    if (action == CONTROLS_ACTION_MOVE)
        return;
    // Circle / Start act as the Android back button (pause, menu back)
    if (keycode == AKEYCODE_BUTTON_B || keycode == AKEYCODE_BUTTON_START)
        nativeKeyEvent(&jni, NULL, AKEYCODE_BACK, action == CONTROLS_ACTION_DOWN);
    gameplay_key(keycode, action == CONTROLS_ACTION_DOWN);
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    switch (action) {
        case CONTROLS_ACTION_DOWN:
            nativeTouchesBegin(&jni, NULL, id, x, y);
            break;
        case CONTROLS_ACTION_UP:
            nativeTouchesEnd(&jni, NULL, id, x, y);
            break;
        case CONTROLS_ACTION_MOVE: {
            // FalsoJNI ignores DeleteLocalRef, so reuse the same arrays
            static jintArray ids = NULL;
            static jfloatArray xs = NULL, ys = NULL;
            if (!ids) {
                ids = jni->NewIntArray(&jni, 1);
                xs = jni->NewFloatArray(&jni, 1);
                ys = jni->NewFloatArray(&jni, 1);
            }
            jni->SetIntArrayRegion(&jni, ids, 0, 1, &id);
            jni->SetFloatArrayRegion(&jni, xs, 0, 1, &x);
            jni->SetFloatArrayRegion(&jni, ys, 0, 1, &y);
            nativeTouchesMove(&jni, NULL, ids, xs, ys);
            break;
        }
    }
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    if (which == CONTROLS_STICK_LEFT)
        gameplay_stick(action == CONTROLS_ACTION_UP ? 0.f : x, action == CONTROLS_ACTION_UP ? 0.f : y);
}
