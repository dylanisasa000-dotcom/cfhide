#include <jni.h>
#include <android/log.h>
#include "zygisk.hpp"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CFHide", __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

class CFHide : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        LOGE("onLoad called!");
    }
    void preAppSpecialize(AppSpecializeArgs *args) override {
        LOGE("preAppSpecialize called!");
    }
    void postAppSpecialize(const AppSpecializeArgs *args) override {
        LOGE("postAppSpecialize called!");
    }
};

REGISTER_ZYGISK_MODULE(CFHide)
