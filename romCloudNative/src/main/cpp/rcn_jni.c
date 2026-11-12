// Pont JNI : charge la bibliothèque d'un jeu transposé (code traduit + runtime), l'exécute, transmet
// sa sortie à l'application (PortSink), affiche ses images dans la surface de l'activité et lui donne
// l'état de la manette. Le jeu tourne sur ses propres threads, créés par le runtime : chacun est
// rattaché à la machine Java à son premier appel, et détaché à sa fin.
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_host.h"

typedef struct {
    JavaVM *vm;
    jobject sink; // référence globale : utilisée depuis les threads du jeu
    jmethodID on_output;
    jmethodID on_video;
    int video_width, video_height; // dernière taille annoncée à l'application
} Sink;

static pthread_key_t attach_key;
static pthread_once_t attach_once = PTHREAD_ONCE_INIT;

// Surface où afficher le jeu (NULL tant que l'activité n'en a pas), et état de la manette.
static pthread_mutex_t window_lock = PTHREAD_MUTEX_INITIALIZER;
static ANativeWindow *window;
static atomic_uint pad_buttons;

// Fin d'un thread du jeu rattaché à la machine Java : il doit s'en détacher.
static void detach_thread(void *vm) { (*(JavaVM *)vm)->DetachCurrentThread(vm); }
static void make_attach_key(void) { pthread_key_create(&attach_key, detach_thread); }

static JNIEnv *thread_env(JavaVM *vm) {
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) return env;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return NULL;
    pthread_once(&attach_once, make_attach_key);
    pthread_setspecific(attach_key, vm);
    return env;
}

static void sink_write(void *user, int stream, const char *data, size_t len) {
    Sink *sink = user;
    JNIEnv *env = thread_env(sink->vm);
    if (!env) return;
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize)len);
    if (!bytes) {
        (*env)->ExceptionClear(env);
        return;
    }
    (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)len, (const jbyte *)data);
    (*env)->CallVoidMethod(env, sink->sink, sink->on_output, (jint)stream, bytes);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, bytes);
}

// Image du jeu : recopiée dans la surface, que le système met à l'échelle de l'écran.
static void sink_present(void *user, const void *pixels, int width, int height, int pitch, int format) {
    Sink *sink = user;
    if (width != sink->video_width || height != sink->video_height) {
        sink->video_width = width;
        sink->video_height = height;
        JNIEnv *env = thread_env(sink->vm);
        if (env) {
            (*env)->CallVoidMethod(env, sink->sink, sink->on_video, (jint)width, (jint)height);
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        }
    }
    pthread_mutex_lock(&window_lock);
    if (window) {
        int rgb565 = format == RCN_PIXEL_RGB565;
        ANativeWindow_setBuffersGeometry(window, width, height, rgb565 ? WINDOW_FORMAT_RGB_565 : WINDOW_FORMAT_RGBX_8888);
        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(window, &buffer, NULL) == 0) {
            int rows = height < buffer.height ? height : buffer.height;
            int columns = width < buffer.width ? width : buffer.width;
            for (int y = 0; y < rows; y++) {
                const uint8_t *src = (const uint8_t *)pixels + (size_t)y * (size_t)pitch;
                uint8_t *dst = (uint8_t *)buffer.bits + (size_t)y * (size_t)buffer.stride * (rgb565 ? 2 : 4);
                if (rgb565) {
                    memcpy(dst, src, (size_t)columns * 2);
                } else {
                    // Le jeu range bleu, vert, rouge ; la surface attend rouge, vert, bleu.
                    for (int x = 0; x < columns; x++) {
                        dst[x * 4] = src[x * 4 + 2];
                        dst[x * 4 + 1] = src[x * 4 + 1];
                        dst[x * 4 + 2] = src[x * 4];
                        dst[x * 4 + 3] = 0xFF;
                    }
                }
            }
            ANativeWindow_unlockAndPost(window);
        }
    }
    pthread_mutex_unlock(&window_lock);
}

static uint32_t sink_pad(void *user) {
    (void)user;
    return atomic_load(&pad_buttons);
}

// Message d'erreur vers Java : NewStringUTF n'accepte que de l'UTF-8 modifié, on s'en tient à l'ASCII.
static jstring ascii_string(JNIEnv *env, const char *text) {
    char clean[512];
    size_t n = 0;
    for (; text && text[n] && n < sizeof clean - 1; n++) {
        clean[n] = (unsigned char)text[n] < 0x80 ? text[n] : '?';
    }
    clean[n] = 0;
    return (*env)->NewStringUTF(env, clean);
}

// Surface de l'activité (null quand elle disparaît).
JNIEXPORT void JNICALL Java_com_romcloud_rcnative_PortRunner_nativeSetSurface(JNIEnv *env, jclass cls, jobject surface) {
    (void)cls;
    ANativeWindow *next = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    pthread_mutex_lock(&window_lock);
    ANativeWindow *previous = window;
    window = next;
    pthread_mutex_unlock(&window_lock);
    if (previous) ANativeWindow_release(previous);
}

// Boutons de la manette enfoncés (bits RCN_PAD_*).
JNIEXPORT void JNICALL Java_com_romcloud_rcnative_PortRunner_nativeSetPad(JNIEnv *env, jclass cls, jint buttons) {
    (void)env;
    (void)cls;
    atomic_store(&pad_buttons, (uint32_t)buttons);
}

JNIEXPORT void JNICALL Java_com_romcloud_rcnative_PortRunner_nativeRun(JNIEnv *env, jclass cls, jstring lib_path,
                                                                        jstring exe_path, jobject sink_obj) {
    (void)cls;
    jclass sink_class = (*env)->GetObjectClass(env, sink_obj);
    jmethodID on_result = (*env)->GetMethodID(env, sink_class, "onResult", "(IILjava/lang/String;)V");
    jmethodID on_output = (*env)->GetMethodID(env, sink_class, "onOutput", "(I[B)V");
    jmethodID on_video = (*env)->GetMethodID(env, sink_class, "onVideo", "(II)V");
    if (!on_result || !on_output || !on_video) return; // exception Java en attente
    // Jamais libérés : des threads du jeu peuvent encore écrire après le retour de cette fonction.
    Sink *sink = calloc(1, sizeof *sink);
    RcnHost *host = calloc(1, sizeof *host);
    if (!sink || !host || (*env)->GetJavaVM(env, &sink->vm) != JNI_OK) return;
    sink->on_output = on_output;
    sink->on_video = on_video;
    sink->sink = (*env)->NewGlobalRef(env, sink_obj);
    host->abi = RCN_HOST_ABI;
    host->user = sink;
    host->write = sink_write;
    host->present = sink_present;
    host->pad = sink_pad;

    const char *lib = (*env)->GetStringUTFChars(env, lib_path, NULL);
    const char *exe = (*env)->GetStringUTFChars(env, exe_path, NULL);
    RcnResult result;
    memset(&result, 0, sizeof result);
    result.status = RCN_ERR_LOAD;

    void *handle = lib && exe && sink->sink ? dlopen(lib, RTLD_NOW | RTLD_LOCAL) : NULL;
    if (!handle) {
        const char *why = dlerror();
        strncpy(result.error, why ? why : "dlopen", sizeof result.error - 1);
    } else {
        int (*run)(const char *, const RcnHost *, RcnResult *) = dlsym(handle, "rcn_port_run");
        if (!run) {
            strncpy(result.error, "rcn_port_run absent de la bibliotheque du jeu", sizeof result.error - 1);
        } else {
            run(exe, host, &result);
        }
        // La bibliothèque reste chargée, et `sink` référencé : des threads du jeu peuvent encore tourner
        // après la fin déclarée par l'un d'eux. Le processus du jeu est arrêté par l'application.
    }
    if (lib) (*env)->ReleaseStringUTFChars(env, lib_path, lib);
    if (exe) (*env)->ReleaseStringUTFChars(env, exe_path, exe);

    jstring error = ascii_string(env, result.error);
    (*env)->CallVoidMethod(env, sink_obj, on_result, (jint)result.status, (jint)result.exit_code, error);
}
