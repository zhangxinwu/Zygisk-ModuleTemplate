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
#include <sstream>
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
#include <thread>
#include <dlfcn.h>

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;


struct suData{
    long long magic; // 0xaabbccdd
    int id;
    char cmd[1000];
};
struct suReply{
    long long magic; // 0xddccbbaa;
    int ret;
};
int comp_fd = -1;

bool isHook = false;
bool isHook1 = false;
bool isHook2 = false;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "mumu", __VA_ARGS__)

char app_data_dir[0x1000];
uint8_t* il2cpp_base = nullptr;
uint8_t* cp = nullptr;

void *(*dlo)(const char *filename, int flag);

void *dlh(const char *filename, int flag) {
    LOGD("DLOPEN: %s", filename);
    return dlo(filename, flag);
}

size_t strlen_hook(char* s) {
    LOGD("strlen %s", s);
    return strlen(s);
}
FILE* fp = nullptr;
void write_thread() {
    char buffer[1000];
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        LOGD("system>> %s", buffer);
    }
}

void check_profile_handle(void *address, DobbyRegisterContext *ctx) {
    if (address == il2cpp_base + 0xCD9640) {
        // strncmp
//        LOGD("strncmp lr 0x%lx -> %s %s %ld", ctx->lr - (uint64_t)il2cpp_base, (char*)ctx->general.regs.x0, (char*)ctx->general.regs.x1, ctx->general.regs.x2);
        if (!isHook2 && strcmp((char*)ctx->general.regs.x0, "VP_LUNARG_minimum_requirements_1_0") == 0 && strcmp((char*)ctx->general.regs.x1, "VP_SUPERCELL_baseline") == 0) {
            isHook2 = true;
            LOGD("do hook!");
            char dobby_p[128];
            __system_property_get("dobby_p", dobby_p);
            auto lp = std::stoll(dobby_p, nullptr, 16);
            char cmd[1000];
            sprintf( cmd, "su -c \"/data/local/tmp/pwatch-aarch64-unknown-linux-musl -t %d x %p \"  > /data/data/com.supercell.moco/pwatch%d.log", gettid(), il2cpp_base + lp, gettid());
            fp = popen(cmd, "r");
            std::thread(write_thread).detach();
            sleep(3);
            LOGD("system cmd %s", cmd);
            LOGD("%p %08x", il2cpp_base + lp, *(uint32_t*)(il2cpp_base + lp));
//            DobbyInstrument(il2cpp_base + 0x955848, check_profile_handle);
//            DobbyInstrument(il2cpp_base + 0x94BD38, check_profile_handle);
        }
    } else {
        LOGD("vp_profile check pc %p, x8 0x%lx x27 0x%lx", address, ctx->general.regs.x8, ctx->general.regs.x27);
    }
}

int strncmp_hook(char* s1, char* s2, int n) {
    return strncmp(s1, s2, n);
}

const char *(*get_soname)(void *);

void *(*call_constructor_origin)(void *);

void *call_constructor_hook(void *so) {
    if (so) {
        const char* name = get_soname(so);
        uint8_t* base = *(uint8_t**)((uint8_t*)so + 0x10);
        if (name && base) {
            if (!isHook1 && strstr(name, "libg.so")) {
                isHook1 = true;
                LOGD("soname %s %p", name, base);
                il2cpp_base = base;
                void* ret = call_constructor_origin(so);
//                DobbyHook(base + 0xCD9640, (void*)strncmp_hook, nullptr);
                DobbyInstrument(base + 0xCD9640, check_profile_handle);
                return ret;
            }
        }
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
//
//        read(fd, &r, sizeof(r));
        if (process && strstr(process, "com.supercell.moco") && !strstr(process, ":") && !isHook) {
            isHook = true;

            LOGD("start %s ok!", process);
//            comp_fd = api->connectCompanion();
//            if (comp_fd == -1) {
//                LOGD("Failed to connect to companion process\n");
//                return;
//            }
//            LOGD("comp_fd %d", comp_fd);

            needClose = false;
//            void *handle = dlopen("libdl.so", RTLD_NOW);
//            DobbyHook(dlsym(handle, "dlopen"), (void *) dlh, (void **) &dlo);

            void *call_constructor = DobbySymbolResolver("linker64",
                                                         "__dl__ZN6soinfo17call_constructorsEv");
            get_soname = (const char *(*)(void *)) DobbySymbolResolver("linker64",
                                                                       "__dl__ZNK6soinfo10get_sonameEv");
            if (call_constructor) {
                DobbyHook(call_constructor, (void *) call_constructor_hook,
                          (void **) &call_constructor_origin);
            }

        }

//        close(fd);
        // Since we do not hook any functions, we should let Zygisk dlclose ourselves
        if (needClose)
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

};

static void companion_handler(int fd) {
    suData data{};
    while (true) {
        if (read(fd, &data, sizeof(data)) == sizeof(data)) {
            LOGD("magic 0x%llx id %d cmd %s", data.magic, data.id, data.cmd);
            system(data.cmd);
            suReply reply{};
            reply.magic = 0xddccbbaa;
            reply.ret = 1;
            write(fd, &reply, sizeof(suReply));
        }
    }
}

// Register our module class and the companion handler function
REGISTER_ZYGISK_MODULE(MyModule)

REGISTER_ZYGISK_COMPANION(companion_handler)