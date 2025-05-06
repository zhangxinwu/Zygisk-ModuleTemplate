/* Copyright 2022-2023 John "topjohnwu" Wu
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <sys/endian.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include "zygisk.hpp"
#include "dirent.h"
#include "dobby.h"
#include <dlfcn.h>

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "mumu", __VA_ARGS__)

char app_data_dir[0x1000];
uint8_t* il2cpp_base = nullptr;
uint8_t* cp = nullptr;

void *(*dlo)(const char *filename, int flag);

void *dlh(const char *filename, int flag) {
    LOGD("DLOPEN: %s", filename);
    return dlo(filename, flag);
}

const char *(*get_soname)(void *);

void *(*call_constructor_origin)(void *);

void *call_constructor_hook(void *so) {
    const char* name = get_soname(so);
    uint8_t* base = *(uint8_t**)((uint8_t*)so + 0x10);
    LOGD("soname %s %p", name, base);
    if (strstr(name, "libnmsssa_core.so")) {
        char cmd[1000];
        memset(cmd, 0, sizeof(cmd));
        sprintf(cmd, "su -c /data/local/tmp/pwatch-aarch64-unknown-linux-musl %d rw4 %p  > %s/pwatch_%d.log &",
            getpid(), base + 0x535c40, app_data_dir, getpid());
        system(cmd);
    }
    return call_constructor_origin(so);
}

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        // Use JNI to fetch our process name
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        const char *app_data_dir_ = env->GetStringUTFChars(args->app_data_dir, nullptr);
        strcpy(app_data_dir, app_data_dir_);
        preSpecialize(process);
        env->ReleaseStringUTFChars(args->nice_name, process);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir_);
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        preSpecialize("system_server");
    }

private:
    Api *api;
    JNIEnv *env;

    void preSpecialize(const char *process) {
        // Demonstrate connecting to companion process
        // We ask the companion for a random number
//        unsigned r = 0;
        bool needClose = true;
//        int fd = api->connectCompanion();
//        if (fd == -1) {
//            LOGD("Failed to connect to companion process\n");
//            return;
//        }
//
//        read(fd, &r, sizeof(r));
        LOGD("start %s", process);
        if (strstr(process, "raven2") || strstr(process, "netease")) {
            LOGD("start %s ok!", process);
            needClose = false;
//            void *handle = dlopen("libdl.so", RTLD_NOW);
//            DobbyHook(dlsym(handle, "dlopen"), (void *) dlh, (void **) &dlo);

            void *call_constructor = DobbySymbolResolver("linker64",
                                                         "__dl__ZN6soinfo17call_constructorsEv");
            get_soname = (const char *(*)(void *)) DobbySymbolResolver("linker64",
                                                                       "__dl__ZNK6soinfo10get_sonameEv");
            DobbyHook(call_constructor, (void *) call_constructor_hook,
                      (void **) &call_constructor_origin);

        }

//        close(fd);
        // Since we do not hook any functions, we should let Zygisk dlclose ourselves
        if (needClose)
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

};

static int urandom = -1;

static void companion_handler(int i) {
    if (urandom < 0) {
        urandom = open("/dev/urandom", O_RDONLY);
    }
    unsigned r;
    read(urandom, &r, sizeof(r));
    //LOGD("companion r=[%u]\n", r);
    write(i, &r, sizeof(r));
}

// Register our module class and the companion handler function
REGISTER_ZYGISK_MODULE(MyModule)

REGISTER_ZYGISK_COMPANION(companion_handler)