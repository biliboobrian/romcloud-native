// Pont JNI : charge la bibliothèque d'un jeu transposé (code traduit + runtime), l'exécute et transmet
// sa sortie à l'application (PortSink). Le jeu tourne sur ses propres threads, créés par le runtime :
// chacun est rattaché à la machine Java à sa première écriture, et détaché à sa fin.
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_host.h"

typedef struct {
    JavaVM *vm;
    jobject sink; // référence globale : utilisée depuis les threads du jeu
    jmethodID on_output;
} Sink;

static pthread_key_t attach_key;
static pthread_once_t attach_once = PTHREAD_ONCE_INIT;

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

JNIEXPORT void JNICALL Java_com_romcloud_rcnative_PortRunner_nativeRun(JNIEnv *env, jclass cls, jstring lib_path,
                                                                        jstring exe_path, jobject sink_obj) {
    (void)cls;
    jclass sink_class = (*env)->GetObjectClass(env, sink_obj);
    jmethodID on_result = (*env)->GetMethodID(env, sink_class, "onResult", "(IILjava/lang/String;)V");
    jmethodID on_output = (*env)->GetMethodID(env, sink_class, "onOutput", "(I[B)V");
    if (!on_result || !on_output) return; // exception Java en attente
    // Jamais libérés : des threads du jeu peuvent encore écrire après le retour de cette fonction.
    Sink *sink = calloc(1, sizeof *sink);
    RcnHost *host = calloc(1, sizeof *host);
    if (!sink || !host || (*env)->GetJavaVM(env, &sink->vm) != JNI_OK) return;
    sink->on_output = on_output;
    sink->sink = (*env)->NewGlobalRef(env, sink_obj);
    host->abi = RCN_HOST_ABI;
    host->user = sink;
    host->write = sink_write;

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
