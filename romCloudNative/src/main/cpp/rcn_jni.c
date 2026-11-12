// Pont JNI : traduit un jeu (recompilateur embarqué), exécute son code traduit avec le runtime, transmet
// sa sortie à l'application (PortSink), affiche ses images dans la surface de l'activité et lui donne
// l'état de la manette. Le jeu tourne sur ses propres threads, créés par le runtime : chacun est
// rattaché à la machine Java à son premier appel, et détaché à sa fin.
#include <aaudio/AAudio.h>
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
static uint32_t window_generation; // change à chaque nouvelle surface
static atomic_uint pad_buttons;
// Axes de la manette (sticks, gâchettes), deux par mot : écrits par l'interface, lus par le jeu.
static atomic_uint pad_axes[3];

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
    // Image dessinée par le GPU (pixels NULL) : seule sa taille compte.
    if (!pixels) return;
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

// Fenêtre pour le rendu par GPU : une référence de plus, que le runtime rend.
static void *sink_window(void *user, uint32_t *generation) {
    (void)user;
    pthread_mutex_lock(&window_lock);
    ANativeWindow *current = window;
    if (current) ANativeWindow_acquire(current);
    *generation = window_generation;
    pthread_mutex_unlock(&window_lock);
    return current;
}

static uint32_t sink_pad(void *user) {
    (void)user;
    return atomic_load(&pad_buttons);
}

static void sink_pad_axes(void *user, int16_t axes[6]) {
    (void)user;
    for (int i = 0; i < 3; i++) {
        uint32_t pair = atomic_load(&pad_axes[i]);
        axes[2 * i] = (int16_t)(pair & 0xFFFF);
        axes[2 * i + 1] = (int16_t)(pair >> 16);
    }
}

// Son : flux AAudio stéréo de 16 bits, alimenté par le mélangeur du jeu depuis le thread de son du système.
static RcnAudioFill audio_fill;
static void *audio_context;

static aaudio_data_callback_result_t audio_callback(AAudioStream *stream, void *user, void *data, int32_t frames) {
    (void)stream;
    (void)user;
    audio_fill(audio_context, data, frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static int sink_audio_open(void *user, int sample_rate, RcnAudioFill fill, void *context) {
    (void)user;
    AAudioStreamBuilder *builder;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return -1;
    audio_fill = fill;
    audio_context = context;
    AAudioStreamBuilder_setSampleRate(builder, sample_rate);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, audio_callback, NULL);
    AAudioStream *stream = NULL;
    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) return -1;
    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        AAudioStream_close(stream);
        return -1;
    }
    return 0; // le flux vit jusqu'à la fin du processus du jeu
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
    window_generation++;
    pthread_mutex_unlock(&window_lock);
    if (previous) ANativeWindow_release(previous);
}

// Boutons de la manette enfoncés (bits RCN_PAD_*).
JNIEXPORT void JNICALL Java_com_romcloud_rcnative_PortRunner_nativeSetPad(JNIEnv *env, jclass cls, jint buttons) {
    (void)env;
    (void)cls;
    atomic_store(&pad_buttons, (uint32_t)buttons);
}

// Axes de la manette : -1 … 1 pour les sticks (y vers le bas), 0 … 1 pour les gâchettes.
static int16_t to_axis(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int16_t)(v < 0 ? v * 32768.0f : v * 32767.0f);
}
JNIEXPORT void JNICALL Java_com_romcloud_rcnative_PortRunner_nativeSetAxes(JNIEnv *env, jclass cls, jfloat lx, jfloat ly, jfloat rx,
                                                                            jfloat ry, jfloat lt, jfloat rt) {
    (void)env;
    (void)cls;
    const float v[6] = {lx, ly, rx, ry, lt, rt};
    for (int i = 0; i < 3; i++) {
        atomic_store(&pad_axes[i], (uint32_t)(uint16_t)to_axis(v[2 * i]) | (uint32_t)(uint16_t)to_axis(v[2 * i + 1]) << 16);
    }
}

// Exécute un jeu : code machine traduit (code_path, .rcnx) et exécutable d'origine (ses données).
JNIEXPORT void JNICALL Java_com_romcloud_rcnative_PortRunner_nativeRun(JNIEnv *env, jclass cls, jstring code_path,
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
    host->audio_open = sink_audio_open;
    host->window = sink_window;
    host->pad_axes = sink_pad_axes;

    const char *code = (*env)->GetStringUTFChars(env, code_path, NULL);
    const char *exe = (*env)->GetStringUTFChars(env, exe_path, NULL);
    RcnResult result;
    memset(&result, 0, sizeof result);
    result.status = RCN_ERR_LOAD;
    if (code && exe && sink->sink) rcn_run_native(code, exe, host, &result);
    if (code) (*env)->ReleaseStringUTFChars(env, code_path, code);
    if (exe) (*env)->ReleaseStringUTFChars(env, exe_path, exe);

    jstring error = ascii_string(env, result.error);
    (*env)->CallVoidMethod(env, sink_obj, on_result, (jint)result.status, (jint)result.exit_code, error);
}

// --- Recompilateur (librcn_recomp.so, Rust) ---

typedef int (*RecompileFn)(const char *, const char *, void (*)(void *, uint32_t, uint32_t), void *, char *, size_t);

static void *recompiler(const char *symbol) {
    static void *handle;
    if (!handle) handle = dlopen("librcn_recomp.so", RTLD_NOW | RTLD_LOCAL);
    return handle ? dlsym(handle, symbol) : NULL;
}

typedef struct {
    JNIEnv *env;
    jobject listener;
    jmethodID on_progress;
} Progress;

static void recompile_progress(void *user, uint32_t done, uint32_t total) {
    Progress *p = user;
    (*p->env)->CallVoidMethod(p->env, p->listener, p->on_progress, (jint)done, (jint)total);
    if ((*p->env)->ExceptionCheck(p->env)) (*p->env)->ExceptionClear(p->env);
}

// Version du recompilateur embarqué, ou null s'il n'est pas chargeable.
JNIEXPORT jstring JNICALL Java_com_romcloud_rcnative_PortRunner_nativeRecompilerVersion(JNIEnv *env, jclass cls) {
    (void)cls;
    const char *(*version)(void) = (const char *(*)(void))recompiler("rcn_recompiler_version");
    return version ? (*env)->NewStringUTF(env, version()) : NULL;
}

// Traduit exe_path en code machine (code_path). Renvoie le bilan ; status[0] reçoit 0 en cas de succès.
JNIEXPORT jstring JNICALL Java_com_romcloud_rcnative_PortRunner_nativeRecompile(JNIEnv *env, jclass cls, jstring exe_path,
                                                                                 jstring code_path, jobject listener,
                                                                                 jintArray status) {
    (void)cls;
    RecompileFn recompile = (RecompileFn)recompiler("rcn_recompile");
    jint code_status = -1;
    char message[4096] = "recompilateur absent de l'application";
    if (recompile) {
        jclass listener_class = (*env)->GetObjectClass(env, listener);
        Progress progress = {env, listener, (*env)->GetMethodID(env, listener_class, "onProgress", "(II)V")};
        const char *exe = (*env)->GetStringUTFChars(env, exe_path, NULL);
        const char *code = (*env)->GetStringUTFChars(env, code_path, NULL);
        if (exe && code && progress.on_progress) {
            code_status = recompile(exe, code, recompile_progress, &progress, message, sizeof message);
        }
        if (exe) (*env)->ReleaseStringUTFChars(env, exe_path, exe);
        if (code) (*env)->ReleaseStringUTFChars(env, code_path, code);
    }
    (*env)->SetIntArrayRegion(env, status, 0, 1, &code_status);
    return (*env)->NewStringUTF(env, message);
}