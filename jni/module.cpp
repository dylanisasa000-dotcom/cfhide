#include <jni.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <link.h>
#include <dlfcn.h>
#include <android/log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "zygisk.hpp"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "CFHide", __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

static bool in_hook = false;
static JavaVM* g_vm = nullptr;
static bool is_target_process = false;

int my_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    int fd = (int)syscall(SYS_openat, dirfd, pathname, flags, mode);
    if (fd < 0 || !pathname) return fd;

    bool is_maps = (strcmp(pathname, "/proc/self/maps") == 0);
    if (!is_maps) {
        char self_maps[64];
        snprintf(self_maps, sizeof(self_maps), "/proc/%d/maps", getpid());
        is_maps = (strcmp(pathname, self_maps) == 0);
    }
    if (!is_maps || in_hook) return fd;
    in_hook = true;

    char *buf = (char*)malloc(4 * 1024 * 1024);
    if (!buf) { in_hook = false; return fd; }
    int total = 0; ssize_t n;
    while ((n = syscall(SYS_read, fd, buf + total, 4 * 1024 * 1024 - 1 - total)) > 0) total += n;
    buf[total] = '\0';
    syscall(SYS_close, fd);

    char *cleaned = (char*)malloc(total + 1);
    cleaned[0] = '\0';
    char *line = strtok(buf, "\n");
    while (line) {
        if (!strstr(line, "libb7x_core.so") && !strstr(line, "zygisk") &&
            !strstr(line, "magisk") && !strstr(line, "cfhide")) {
            strcat(cleaned, line);
            strcat(cleaned, "\n");
        }
        line = strtok(NULL, "\n");
    }
    free(buf);

    int memfd = (int)syscall(__NR_memfd_create, "maps_clean", 0);
    if (memfd >= 0) {
        syscall(SYS_write, memfd, cleaned, strlen(cleaned));
        syscall(SYS_lseek, memfd, 0, SEEK_SET);
        free(cleaned);
        in_hook = false;
        return memfd;
    }
    free(cleaned);
    in_hook = false;
    return fd;
}

static void install_hook(void *target, void *replacement) {
    uintptr_t page = (uintptr_t)target & ~0xFFFULL;
    mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);
    uint32_t patch[2] = { 0x58000050u, 0xD61F0200u };
    memcpy(target, patch, 8);
    memcpy((char*)target + 8, &replacement, 8);
    __builtin___clear_cache((char*)page, (char*)page + 0x1000);
    mprotect((void*)page, 0x1000, PROT_READ | PROT_EXEC);
}

void* unlink_thread(void*) {
    void *handle = nullptr;
    for (int i = 0; i < 30; i++) {
        handle = dlopen("libb7x_core.so", RTLD_NOLOAD | RTLD_LAZY);
        if (handle) break;
        sleep(1);
    }
    if (!handle) return nullptr;

    struct link_map *map = (struct link_map *)handle;
    if (map && map->l_prev) {
        if (map->l_prev) map->l_prev->l_next = map->l_next;
        if (map->l_next) map->l_next->l_prev = map->l_prev;
        LOGD("unlinked libb7x_core.so from link_map");
    }
    return nullptr;
}

void* inject_thread(void*) {
    JNIEnv* env = nullptr;
    g_vm->AttachCurrentThread(&env, nullptr);
    if (!env) return nullptr;

    LOGD("inject_thread started");

    while (true) {
        jclass atClass = env->FindClass("android/app/ActivityThread");
        if (atClass) {
            jmethodID currentAT = env->GetStaticMethodID(atClass, "currentActivityThread", "()Landroid/app/ActivityThread;");
            jobject at = env->CallStaticObjectMethod(atClass, currentAT);
            if (at) {
                jmethodID getApp = env->GetMethodID(atClass, "getApplication", "()Landroid/app/Application;");
                jobject app = env->CallObjectMethod(at, getApp);
                if (app) {
                    LOGD("Found Application context");
                    jclass ctxClass = env->FindClass("android/content/Context");
                    jmethodID getCL = env->GetMethodID(ctxClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
                    jobject cl = env->CallObjectMethod(app, getCL);
                    
                    if (cl) {
                        LOGD("Found ClassLoader");
                        jclass clClass = env->FindClass("java/lang/ClassLoader");
                        jmethodID loadClass = env->GetMethodID(clClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                        
                        jclass upClass = (jclass)env->CallObjectMethod(cl, loadClass, env->NewStringUTF("com.unity3d.player.UnityPlayer"));
                        if (upClass) {
                            LOGD("Found UnityPlayer class");
                            jfieldID fid = env->GetStaticFieldID(upClass, "currentActivity", "Landroid/app/Activity;");
                            if (fid) {
                                jobject activity = env->GetStaticObjectField(upClass, fid);
                                if (activity) {
                                    LOGD("Found Unity Activity");
                                    
                                    jstring dexPath = env->NewStringUTF("/data/adb/modules/zygisk_cfhide/classes3.dex");
                                    jstring optDir = env->NewStringUTF("/data/local/tmp/");
                                    jclass dexCLClass = env->FindClass("dalvik/system/DexClassLoader");
                                    jmethodID dexInit = env->GetMethodID(dexCLClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
                                    jobject dexCL = env->NewObject(dexCLClass, dexInit, dexPath, optDir, nullptr, cl);
                                    
                                    jclass overlayClass = (jclass)env->CallObjectMethod(dexCL, loadClass, env->NewStringUTF("com.platinmods.counterflame.TrainingOverlay"));
                                    if (overlayClass) {
                                        LOGD("Loaded TrainingOverlay class");
                                        jmethodID startM = env->GetStaticMethodID(overlayClass, "start", "(Landroid/app/Activity;)V");
                                        if (!startM) startM = env->GetStaticMethodID(overlayClass, "init", "(Landroid/app/Activity;)V");
                                        if (!startM) startM = env->GetStaticMethodID(overlayClass, "attach", "(Landroid/app/Activity;)V");
                                        if (!startM) startM = env->GetStaticMethodID(overlayClass, "setup", "(Landroid/app/Activity;)V");
                                        
                                        if (startM) {
                                            LOGD("Called TrainingOverlay init");
                                            env->CallStaticVoidMethod(overlayClass, startM, activity);
                                            break;
                                        } else {
                                            LOGD("Could not find init method");
                                        }
                                    } else {
                                        LOGD("Failed to load TrainingOverlay class");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        sleep(1);
    }
    g_vm->DetachCurrentThread();
    return nullptr;
}

class CFHide : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        env->GetJavaVM(&g_vm);
        LOGD("onLoad called");
    }
    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args->nice_name) return;
        JNIEnv *env = nullptr;
        g_vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        const char *nice = env->GetStringUTFChars(args->nice_name, nullptr);
        
        if (nice) {
            LOGD("process: %s", nice);
            if (strstr(nice, "com.ScarletWork") != nullptr) {
                LOGD("Target process found!");
                is_target_process = true;
            }
            env->ReleaseStringUTFChars(args->nice_name, nice);
        }

        if (!is_target_process) return;

        void *openat_sym = dlsym(RTLD_DEFAULT, "openat");
        if (openat_sym) install_hook(openat_sym, (void*)my_openat);
        
        pthread_t t1;
        pthread_create(&t1, nullptr, unlink_thread, nullptr);
        pthread_detach(t1);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (is_target_process) {
            LOGD("postAppSpecialize reached.");
            pthread_t t;
            pthread_create(&t, nullptr, inject_thread, nullptr);
            pthread_detach(t);
        }
    }
private:
    Api *api = nullptr;
};

REGISTER_ZYGISK_MODULE(CFHide)
