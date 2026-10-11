#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/quest-pe-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    unlink(path);
    size_t size = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char* bytes = calloc(1, size);
    assert(bytes);
    bytes[0] = 'M'; bytes[1] = 'Z'; bytes[60] = 128;
    memcpy(bytes + 128, "PE\0\0", 4);
    assert(write(fd, bytes, size) == (ssize_t)size);
    void* reserved = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(reserved != MAP_FAILED);
    errno = 0;
    assert(mmap(reserved, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0) == MAP_FAILED);
    assert(errno == EACCES);
    // The same anonymous fallback Wine uses remains executable and intact.
    assert(mmap(reserved, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) == reserved);
    assert(pread(fd, reserved, size, 0) == (ssize_t)size);
    assert(mprotect(reserved, size, PROT_READ | PROT_EXEC) == 0);
    assert(memcmp(reserved, bytes, size) == 0);
    // Read-only PE headers and ordinary fixed file mappings are untouched.
    assert(mmap(reserved, size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0) == reserved);
    bytes[0] = 'X';
    assert(pwrite(fd, bytes, size, 0) == (ssize_t)size);
    assert(mmap(reserved, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0) == reserved);
    // mmap64 must preserve offsets above 4 GiB (the test file is sparse).
    off64_t offset = (off64_t)5 * 1024 * 1024 * 1024;
    assert(pwrite64(fd, bytes, size, offset) == (ssize_t)size);
    assert(mmap64(reserved, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, offset) == reserved);
    assert(memcmp(reserved, bytes, size) == 0);
    munmap(reserved, size);
    free(bytes);
    close(fd);
    return 0;
}
