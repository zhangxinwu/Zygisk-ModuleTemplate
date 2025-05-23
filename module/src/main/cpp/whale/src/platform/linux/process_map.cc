#include <string>
#include <base/logging.h>
#include <climits>
#include "platform/linux/process_map.h"
#include "process_map.h"


namespace whale {

std::unique_ptr<MemoryRange> FindFileMemoryRange(const char *name) {
    std::unique_ptr<MemoryRange> range(new MemoryRange);
    range->base_ = UINTPTR_MAX;
    ForeachMemoryRange(
            [&](uintptr_t begin, uintptr_t end, uintptr_t offset, char *perm, char *mapname) -> bool {
                if (strstr(mapname, name)) {
                    if (range->path_ == nullptr) {
                        range->path_ = strdup(mapname);
                    }
                    if (range->base_ > begin) {
                        range->base_ = begin;
                    }
                    if (range->end_ < end) {
                        range->end_ = end;
                    }
                }
                return true;
            });
    return range;
}

std::unique_ptr<MemoryRange> FindExecuteMemoryRange(const char *name) {
    std::unique_ptr<MemoryRange> range(new MemoryRange);
    ForeachMemoryRange(
            [&](uintptr_t begin, uintptr_t end, uintptr_t offset, char *perm, char *mapname) -> bool {
                if (strncmp(mapname, "/system/fake-libs/", 18) == 0) {
                    return true;
                }
                if (strstr(mapname, name)) {
                    //find the correct base address
                    if (offset == 0) {
                        range->path_ = strdup(mapname);
                        range->base_ = begin;
                        range->end_ = end;
                    }
//                    if (strstr(perm, "x"))
//                        return false;
                    if (perm[0] == 'r' && perm[3] == 'p')
                        return false;
                }
                return true;
            });
    return range;
}

void
ForeachMemoryRange(std::function<bool(uintptr_t, uintptr_t, uintptr_t offset, char *, char *)> callback) {
    FILE *f;
    if ((f = fopen("/proc/self/maps", "r"))) {
        char buf[PATH_MAX], perm[12] = {'\0'}, dev[12] = {'\0'}, mapname[PATH_MAX] = {'\0'};
        uintptr_t begin, end, inode, foo;

        while (!feof(f)) {
            if (fgets(buf, sizeof(buf), f) == 0)
                break;
            sscanf(buf, "%lx-%lx %s %lx %s %ld %s", &begin, &end, perm,
                   &foo, dev, &inode, mapname);
            if (!callback(begin, end, foo, perm, mapname)) {
                break;
            }
        }
        fclose(f);
    }
}

bool IsFileInMemory(const char *name) {
    bool found = false;
    ForeachMemoryRange(
            [&](uintptr_t begin, uintptr_t end, uintptr_t offset, char *perm, char *mapname) -> bool {
                if (strstr(mapname, name)) {
                    found = true;
                    return false;
                }
                return true;
            });
    return found;
}

}  // namespace whale
