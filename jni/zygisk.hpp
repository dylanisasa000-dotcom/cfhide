#pragma once

#include <jni.h>
#include <sys/types.h>

#define ZYGISK_API_VERSION 5

namespace zygisk {

struct Api;
struct AppSpecializeArgs;
struct ServerSpecializeArgs;

class ModuleBase {
public:
    virtual void onLoad(Api *api, JNIEnv *env) {}
    virtual void preAppSpecialize(AppSpecializeArgs *args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs *args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs *args) {}
};

struct AppSpecializeArgs {
    jstring nice_name;
    jstring app_data_dir;
    jobjectArray app_zygote;
    jint mount_external;
    jint rlimits;
    jintArray gids;
    jintArray permitted_capabilities;
    jintArray effective_capabilities;
    jintArray bounding_capabilities;
    jint target_sdk_version;
    jstring instruction_set;
    jstring app_data_dir_2;
    jboolean is_top_app;
    jstring se_info;
    jstring nice_name_2;
};

struct ServerSpecializeArgs {
    jint gids;
    jintArray gids_array;
    jintArray permitted_capabilities;
    jintArray effective_capabilities;
    jintArray bounding_capabilities;
};

struct Api {
    int connectCompanion();
    void setOption(int option);
    JNIEnv *getEnv();
    void (*preAppSpecialize)(void *, void *);
    void (*postAppSpecialize)(void *, const void *);
    void (*preServerSpecialize)(void *, void *);
    void (*postServerSpecialize)(void *, const void *);
};

struct module_abi {
    long api_version;
    ModuleBase *impl;
    module_abi(ModuleBase *module) : api_version(ZYGISK_API_VERSION), impl(module) {
        api.preAppSpecialize = [](auto m, auto args) { ((ModuleBase *)m)->preAppSpecialize((AppSpecializeArgs *)args); };
        api.postAppSpecialize = [](auto m, auto args) { ((ModuleBase *)m)->postAppSpecialize((const AppSpecializeArgs *)args); };
        api.preServerSpecialize = [](auto m, auto args) { ((ModuleBase *)m)->preServerSpecialize((ServerSpecializeArgs *)args); };
        api.postServerSpecialize = [](auto m, auto args) { ((ModuleBase *)m)->postServerSpecialize((const ServerSpecializeArgs *)args); };
    }
    Api api;
};

} // namespace zygisk

#define REGISTER_ZYGISK_MODULE(cls) \
    extern "C" [[gnu::visibility("default")]] void zygisk_module_entry(zygisk::module_abi *abi) { \
        abi->impl = new cls(); \
    }
