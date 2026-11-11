// Pont JNI : charge la bibliothèque d'un jeu transposé (code traduit + runtime), l'exécute sur le
// thread appelant et transmet sa sortie à l'application (PortSink).
#include <dlfcn.h>
#include <jni.h>
#include <string.h>

#include "rcn_host.h"

typedef struct {
    JNIEnv *env;
    jobject sink;
    jmethodID on_output;
} Sink;

static void sink_write(void *user, int stream, const char *data, size_t len) {
    Sink *sink = user;
    JNIEnv *env = sink->env;
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
    Sink sink = {env, sink_obj, (*env)->GetMethodID(env, sink_class, "onOutput", "(I[B)V")};
    if (!on_result || !sink.on_output) return; // exception Java en attente

    const char *lib = (*env)->GetStringUTFChars(env, lib_path, NULL);
    const char *exe = (*env)->GetStringUTFChars(env, exe_path, NULL);
    RcnResult result;
    memset(&result, 0, sizeof result);
    result.status = RCN_ERR_LOAD;

    void *handle = lib && exe ? dlopen(lib, RTLD_NOW | RTLD_LOCAL) : NULL;
    if (!handle) {
        const char *why = dlerror();
        strncpy(result.error, why ? why : "dlopen", sizeof result.error - 1);
    } else {
        int (*run)(const char *, const RcnHost *, RcnResult *) = dlsym(handle, "rcn_port_run");
        if (!run) {
            strncpy(result.error, "rcn_port_run absent de la bibliotheque du jeu", sizeof result.error - 1);
        } else {
            RcnHost host = {RCN_HOST_ABI, &sink, sink_write};
            run(exe, &host, &result);
        }
        dlclose(handle);
    }
    if (lib) (*env)->ReleaseStringUTFChars(env, lib_path, lib);
    if (exe) (*env)->ReleaseStringUTFChars(env, exe_path, exe);

    jstring error = ascii_string(env, result.error);
    (*env)->CallVoidMethod(env, sink_obj, on_result, (jint)result.status, (jint)result.exit_code, error);
}
