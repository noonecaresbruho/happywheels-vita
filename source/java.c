#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include "java.h"
#include "utils/logger.h"

/*
 * SharedPreferences replacement (backs cocos2d::UserDefault on Android).
 * Kept in memory and flushed to PREFS_PATH by prefs_flush() when dirty.
 */

#define PREFS_PATH DATA_PATH "prefs.txt"

typedef struct {
    char *key;
    char *value; // always stored as text
} pref_t;

static pref_t *prefs = NULL;
static int prefs_count = 0;
static int prefs_cap = 0;
static int prefs_dirty = 0;
static pthread_mutex_t prefs_lock = PTHREAD_MUTEX_INITIALIZER;

static void unescape(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '\\' && r[1]) {
            r++;
            *w++ = (*r == 'n') ? '\n' : (*r == 't') ? '\t' : *r;
        } else {
            *w++ = *r;
        }
    }
    *w = 0;
}

static void write_escaped(FILE *f, const char *s) {
    for (; *s; s++) {
        if (*s == '\n') fputs("\\n", f);
        else if (*s == '\t') fputs("\\t", f);
        else if (*s == '\\') fputs("\\\\", f);
        else fputc(*s, f);
    }
}

static pref_t *pref_find(const char *key) {
    for (int i = 0; i < prefs_count; i++)
        if (strcmp(prefs[i].key, key) == 0)
            return &prefs[i];
    return NULL;
}

static void pref_set(const char *key, const char *value) {
    pthread_mutex_lock(&prefs_lock);
    pref_t *p = pref_find(key);
    if (p) {
        if (strcmp(p->value, value) != 0) {
            free(p->value);
            p->value = strdup(value);
            prefs_dirty = 1;
        }
    } else {
        if (prefs_count == prefs_cap) {
            prefs_cap = prefs_cap ? prefs_cap * 2 : 64;
            prefs = realloc(prefs, prefs_cap * sizeof(pref_t));
        }
        prefs[prefs_count].key = strdup(key);
        prefs[prefs_count].value = strdup(value);
        prefs_count++;
        prefs_dirty = 1;
    }
    pthread_mutex_unlock(&prefs_lock);
}

static void pref_delete(const char *key) {
    pthread_mutex_lock(&prefs_lock);
    pref_t *p = pref_find(key);
    if (p) {
        free(p->key);
        free(p->value);
        *p = prefs[--prefs_count];
        prefs_dirty = 1;
    }
    pthread_mutex_unlock(&prefs_lock);
}

void prefs_load() {
    FILE *f = fopen(PREFS_PATH, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);

    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = 0;
            unescape(line);
            unescape(tab + 1);
            pref_set(line, tab + 1);
        }
        line = next;
    }
    free(buf);
    prefs_dirty = 0;
    l_info("Loaded %i preferences.", prefs_count);
}

void prefs_flush() {
    pthread_mutex_lock(&prefs_lock);
    if (prefs_dirty) {
        FILE *f = fopen(PREFS_PATH ".tmp", "wb");
        if (f) {
            for (int i = 0; i < prefs_count; i++) {
                write_escaped(f, prefs[i].key);
                fputc('\t', f);
                write_escaped(f, prefs[i].value);
                fputc('\n', f);
            }
            fclose(f);
            sceIoRemove(PREFS_PATH);
            sceIoRename(PREFS_PATH ".tmp", PREFS_PATH);
            prefs_dirty = 0;
        }
    }
    pthread_mutex_unlock(&prefs_lock);
}

// Copies a jstring's contents into a fresh malloc'd C string.
static char *jstr_dup(jstring s) {
    if (!s) return strdup("");
    const char *c = jni->GetStringUTFChars(&jni, s, NULL);
    char *r = strdup(c ? c : "");
    jni->ReleaseStringUTFChars(&jni, s, c);
    return r;
}

static const char *pref_get(jstring _key) {
    char *key = jstr_dup(_key);
    pthread_mutex_lock(&prefs_lock);
    pref_t *p = pref_find(key);
    const char *v = p ? p->value : NULL;
    pthread_mutex_unlock(&prefs_lock);
    free(key);
    return v;
}

jboolean getBoolForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    jboolean def = (jboolean)va_arg(args, int);
    const char *v = pref_get(key);
    return v ? (jboolean)atoi(v) : def;
}

jint getIntegerForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    jint def = va_arg(args, jint);
    const char *v = pref_get(key);
    return v ? (jint)strtol(v, NULL, 10) : def;
}

jfloat getFloatForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    jfloat def = (jfloat)va_arg(args, double);
    const char *v = pref_get(key);
    return v ? (jfloat)strtod(v, NULL) : def;
}

jdouble getDoubleForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    jdouble def = va_arg(args, double);
    const char *v = pref_get(key);
    return v ? strtod(v, NULL) : def;
}

jobject getStringForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    jstring def = va_arg(args, jstring);
    const char *v = pref_get(key);
    if (!v) {
        if (!def) return NULL;
        char *d = jstr_dup(def);
        jstring r = jni->NewStringUTF(&jni, d);
        free(d);
        return r;
    }
    return jni->NewStringUTF(&jni, v);
}

static void set_from_key(jstring _key, const char *value) {
    char *key = jstr_dup(_key);
    pref_set(key, value);
    free(key);
}

void setBoolForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    int v = va_arg(args, int);
    set_from_key(key, v ? "1" : "0");
}

void setIntegerForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    char buf[32];
    snprintf(buf, sizeof(buf), "%i", va_arg(args, jint));
    set_from_key(key, buf);
}

void setFloatForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.9g", va_arg(args, double));
    set_from_key(key, buf);
}

void setDoubleForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", va_arg(args, double));
    set_from_key(key, buf);
}

void setStringForKey(jmethodID id, va_list args) {
    jstring key = va_arg(args, jstring);
    char *v = jstr_dup(va_arg(args, jstring));
    set_from_key(key, v);
    free(v);
}

void deleteValueForKey(jmethodID id, va_list args) {
    char *key = jstr_dup(va_arg(args, jstring));
    pref_delete(key);
    free(key);
}

/*
 * org.cocos2dx.lib.Cocos2dxHelper
 */

jobject getCocos2dxPackageName(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "com.fancyforce.happywheels");
}

jobject getCocos2dxWritablePath(jmethodID id, va_list args) {
    // Translated to DATA_PATH "files" by the IO layer, see reimpl/io.c
    return jni->NewStringUTF(&jni, "/hw/files");
}

jobject getAssetsPath(jmethodID id, va_list args) {
    // Must not contain "/obb/", otherwise Cocos tries to open it as a zip.
    return jni->NewStringUTF(&jni, "/hw/base.apk");
}

jobject getCurrentLanguage(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "en");
}

jobject getDeviceModel(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "PS Vita");
}

jobject getVersion(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "1.1.2");
}

jint getDPI(jmethodID id, va_list args) {
    return 220; // 5" 960x544
}

jint getSDKVersion(jmethodID id, va_list args) {
    return 19;
}

jboolean retFalse(jmethodID id, va_list args) {
    return JNI_FALSE;
}

jobject retNull(jmethodID id, va_list args) {
    return NULL;
}

void noop(jmethodID id, va_list args) {
}

volatile float animation_interval = 1.0f / 60.0f;

void setAnimationInterval(jmethodID id, va_list args) {
    animation_interval = (float)va_arg(args, double);
}

void terminateProcess(jmethodID id, va_list args) {
    prefs_flush();
    sceKernelExitProcess(0);
}

/*
 * android.content.Context / java.lang.ClassLoader
 * JniHelper resolves every class through Context.getClassLoader().loadClass()
 */

jobject getClassLoader(jmethodID id, va_list args) {
    return (jobject)0x43434343;
}

jobject loadClass(jmethodID id, va_list args) {
    static char *classes[128];
    static int classes_count = 0;

    char *name = jstr_dup(va_arg(args, jstring));
    for (char *c = name; *c; c++)
        if (*c == '.') *c = '/';

    // Same representation as FalsoJNI's FindClass (class name pointer),
    // cached since JniHelper looks classes up on every call.
    for (int i = 0; i < classes_count; i++) {
        if (strcmp(classes[i], name) == 0) {
            free(name);
            return (jobject)classes[i];
        }
    }
    if (classes_count < 128)
        classes[classes_count++] = name;
    return (jobject)name;
}

/*
 * JNI Methods
*/

NameToMethodID nameToMethodId[] = {
    // UserDefault
    { 1, "getBoolForKey", METHOD_TYPE_BOOLEAN },
    { 2, "getIntegerForKey", METHOD_TYPE_INT },
    { 3, "getFloatForKey", METHOD_TYPE_FLOAT },
    { 4, "getDoubleForKey", METHOD_TYPE_DOUBLE },
    { 5, "getStringForKey", METHOD_TYPE_OBJECT },
    { 6, "setBoolForKey", METHOD_TYPE_VOID },
    { 7, "setIntegerForKey", METHOD_TYPE_VOID },
    { 8, "setFloatForKey", METHOD_TYPE_VOID },
    { 9, "setDoubleForKey", METHOD_TYPE_VOID },
    { 10, "setStringForKey", METHOD_TYPE_VOID },
    { 11, "deleteValueForKey", METHOD_TYPE_VOID },

    // Cocos2dxHelper
    { 20, "getCocos2dxPackageName", METHOD_TYPE_OBJECT },
    { 21, "getCocos2dxWritablePath", METHOD_TYPE_OBJECT },
    { 22, "getAssetsPath", METHOD_TYPE_OBJECT },
    { 23, "getCurrentLanguage", METHOD_TYPE_OBJECT },
    { 24, "getCurrentLanguageCode", METHOD_TYPE_OBJECT },
    { 25, "getDeviceModel", METHOD_TYPE_OBJECT },
    { 26, "getVersion", METHOD_TYPE_OBJECT },
    { 27, "getDPI", METHOD_TYPE_INT },
    { 28, "getSDKVersion", METHOD_TYPE_INT },
    { 29, "isScreenRound", METHOD_TYPE_BOOLEAN },
    { 30, "isCutoutEnabled", METHOD_TYPE_BOOLEAN },
    { 31, "getSafeInsets", METHOD_TYPE_OBJECT },
    { 32, "setKeepScreenOn", METHOD_TYPE_VOID },
    { 33, "vibrate", METHOD_TYPE_VOID },
    { 34, "openURL", METHOD_TYPE_BOOLEAN },
    { 35, "terminateProcess", METHOD_TYPE_VOID },
    { 36, "setAnimationInterval", METHOD_TYPE_VOID },
    { 37, "enableAccelerometer", METHOD_TYPE_VOID },
    { 38, "disableAccelerometer", METHOD_TYPE_VOID },
    { 39, "setAccelerometerInterval", METHOD_TYPE_VOID },
    { 40, "openIMEKeyboard", METHOD_TYPE_VOID },
    { 41, "closeIMEKeyboard", METHOD_TYPE_VOID },
    { 42, "getObbAssetFileDescriptor", METHOD_TYPE_OBJECT },

    { 50, "getClassLoader", METHOD_TYPE_OBJECT },
    { 51, "loadClass", METHOD_TYPE_OBJECT },
};

MethodsBoolean methodsBoolean[] = {
    { 1, getBoolForKey },
    { 29, retFalse },
    { 30, retFalse },
    { 34, retFalse },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {
    { 4, getDoubleForKey },
};
MethodsFloat methodsFloat[] = {
    { 3, getFloatForKey },
};
MethodsInt methodsInt[] = {
    { 2, getIntegerForKey },
    { 27, getDPI },
    { 28, getSDKVersion },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 5, getStringForKey },
    { 20, getCocos2dxPackageName },
    { 21, getCocos2dxWritablePath },
    { 22, getAssetsPath },
    { 23, getCurrentLanguage },
    { 24, getCurrentLanguage },
    { 25, getDeviceModel },
    { 26, getVersion },
    { 31, retNull },
    { 42, retNull },
    { 50, getClassLoader },
    { 51, loadClass },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 6, setBoolForKey },
    { 7, setIntegerForKey },
    { 8, setFloatForKey },
    { 9, setDoubleForKey },
    { 10, setStringForKey },
    { 11, deleteValueForKey },
    { 32, noop },
    { 33, noop },
    { 35, terminateProcess },
    { 36, setAnimationInterval },
    { 37, noop },
    { 38, noop },
    { 39, noop },
    { 40, noop },
    { 41, noop },
};

/*
 * JNI Fields
*/

// System-wide constant that applications sometimes request
// https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE
char WINDOW_SERVICE[] = "window";

// System-wide constant that's often used to determine Android version
// https://developer.android.com/reference/android/os/Build.VERSION.html#SDK_INT
// Possible values: https://developer.android.com/reference/android/os/Build.VERSION_CODES
const int SDK_INT = 19; // Android 4.4 / KitKat

NameToFieldID nameToFieldId[] = {
		{ 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
		{ 1, "SDK_INT", FIELD_TYPE_INT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
		{ 1, SDK_INT },
};
FieldsObject fieldsObject[] = {
		{ 0, WINDOW_SERVICE },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
