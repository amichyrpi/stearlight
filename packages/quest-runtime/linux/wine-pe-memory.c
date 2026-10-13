// Android rejects executable private mappings of writable PE image files.
// Decline only Wine's initial fixed PE-header mapping so Wine uses its own
// anonymous image/read fallback, including its normal section protections.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

static int private_pe(int protection, int flags, int fd, off64_t offset) {
    if (fd < 0 || offset || !(protection & PROT_WRITE) ||
        !(flags & MAP_FIXED) || !(flags & MAP_PRIVATE) || (flags & MAP_ANONYMOUS)) return 0;
    unsigned char dos[64], signature[4];
    if (pread(fd, dos, sizeof(dos), 0) != (ssize_t)sizeof(dos) ||
        dos[0] != 'M' || dos[1] != 'Z') return 0;
    uint32_t pe = (uint32_t)dos[60] | ((uint32_t)dos[61] << 8) |
        ((uint32_t)dos[62] << 16) | ((uint32_t)dos[63] << 24);
    return pread(fd, signature, 4, pe) == 4 && signature[0] == 'P' &&
        signature[1] == 'E' && !signature[2] && !signature[3];
}

void* mmap(void* address, size_t size, int protection, int flags, int fd, off_t offset) {
    static void* (*next)(void*, size_t, int, int, int, off_t);
    if (private_pe(protection, flags, fd, offset)) { errno = EACCES; return MAP_FAILED; }
    void* (*function)(void*, size_t, int, int, int, off_t) = __atomic_load_n(&next, __ATOMIC_ACQUIRE);
    if (!function) {
        function = dlsym(RTLD_NEXT, "mmap");
        if (!function) { errno = ENOSYS; return MAP_FAILED; }
        __atomic_store_n(&next, function, __ATOMIC_RELEASE);
    }
    return function(address, size, protection, flags, fd, offset);
}

void* mmap64(void* address, size_t size, int protection, int flags, int fd, off64_t offset) {
    static void* (*next)(void*, size_t, int, int, int, off64_t);
    if (private_pe(protection, flags, fd, offset)) { errno = EACCES; return MAP_FAILED; }
    void* (*function)(void*, size_t, int, int, int, off64_t) = __atomic_load_n(&next, __ATOMIC_ACQUIRE);
    if (!function) {
        function = dlsym(RTLD_NEXT, "mmap64");
        if (!function) { errno = ENOSYS; return MAP_FAILED; }
        __atomic_store_n(&next, function, __ATOMIC_RELEASE);
    }
    return function(address, size, protection, flags, fd, offset);
}
