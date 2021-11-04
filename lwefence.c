#ifndef DDEBUG
#define DDEBUG 1
#endif

#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if DDEBUG
#define dd(...)                    \
    fprintf(stderr, "lwefence: "); \
    fprintf(stderr, __VA_ARGS__);
#else
#define dd(...)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Rounding operations (efficient when n is a power of 2)
// Round down to the nearest multiple of n
#define ROUNDDOWN(a, n)                         \
    ({                                          \
        unsigned long __a = (unsigned long)(a); \
        (typeof(a))(__a - __a % (n));           \
    })
// Round up to the nearest multiple of n
#define ROUNDUP(a, n)                                              \
    ({                                                             \
        unsigned long __n = (unsigned long)(n);                    \
        (typeof(a))(ROUNDDOWN((unsigned long)(a) + __n - 1, __n)); \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define MAGICNUM (0xdeadbeef)
#define RESIZE(size) (ROUNDUP((size), sizeof(unsigned long)) + sizeof(lwe_node) + 2 * getpagesize())

enum {
    LWE_RIGHT_OVERFLOW_MODE,
    LWE_LEFT_OVERFLOW_MODE,
};

typedef struct {
    unsigned long magic_value;
    unsigned long real_addr;
    unsigned long real_size;
    unsigned long align_addr;
} lwe_node;

void *__libc_malloc(size_t size);
void __libc_free(void *ptr);

/*
 * EF_PROTECT_BELOW is used to modify the behavior of the allocator. When
 * its value is non-zero, the allocator will place an inaccessable page
 * immediately _before_ the malloc buffer in the address space, instead
 * of _after_ it. Use this to detect malloc buffer under-runs, rather than
 * over-runs. It won't detect both at the same time, so you should test your
 * software twice, once with this value clear, and once with it set.
 * If the value is -1, it will be set from the environment or to zero at
 * run-time
 */
int EF_PROTECT_BELOW = LWE_RIGHT_OVERFLOW_MODE;

static void *__lwe_setup_overflow(unsigned long real_addr, unsigned long real_size, unsigned long user_size)
{
    unsigned long align_addr;
    unsigned long user_addr;
    lwe_node *pNode;

    if (EF_PROTECT_BELOW == LWE_RIGHT_OVERFLOW_MODE) {
        align_addr = ROUNDDOWN(real_addr + real_size, getpagesize()) - getpagesize();
        user_addr = align_addr - user_size;
        pNode = (lwe_node *)ROUNDDOWN(user_addr, sizeof(unsigned long)) - 1;
    } else if (EF_PROTECT_BELOW == LWE_LEFT_OVERFLOW_MODE) {
        align_addr = ROUNDUP(real_addr + sizeof(lwe_node), getpagesize());
        user_addr = align_addr + getpagesize();
        pNode = (lwe_node *)ROUNDDOWN(user_addr - getpagesize(), sizeof(unsigned long)) - 1;
    } else {
        assert(0);
    }

    if (mprotect((void *)align_addr, getpagesize(), PROT_NONE) < 0) {
        dd("%s\n", strerror(errno));
        assert(0);
    }

    dd("0x%zx 0x%zx 0x%zx 0x%zx 0x%zx\n", real_addr, real_size, user_addr, user_size, align_addr);
    pNode->magic_value = MAGICNUM;
    pNode->real_addr = real_addr;
    pNode->real_size = real_size;
    pNode->align_addr = align_addr;

    return (void *)user_addr;
}

static void *__lwe_cancel_overflow(unsigned long user_addr)
{
    unsigned long align_addr;
    unsigned long real_addr;
    lwe_node *pNode = NULL;

    if (EF_PROTECT_BELOW == LWE_RIGHT_OVERFLOW_MODE) {
        pNode = (lwe_node *)ROUNDDOWN(user_addr, sizeof(unsigned long)) - 1;
    } else if (EF_PROTECT_BELOW == LWE_LEFT_OVERFLOW_MODE) {
        pNode = (lwe_node *)ROUNDDOWN(user_addr - getpagesize(), sizeof(unsigned long)) - 1;
    } else {
        assert(0);
    }

    assert(pNode->magic_value == MAGICNUM);
    align_addr = pNode->align_addr;
    real_addr = pNode->real_addr;

    if (mprotect((void *)align_addr, getpagesize(), PROT_READ | PROT_WRITE) < 0) {
        dd("[%s]%s\n", __func__, strerror(errno));
        assert(0);
    }

    return (void *)real_addr;
}

static void *__lwe_malloc(size_t user_size)
{
    void *real_addr;
    size_t real_size;

    real_size = RESIZE(user_size);
    real_addr = __libc_malloc(real_size);
    if (real_addr == NULL) {
        return NULL;
    }

    return __lwe_setup_overflow((unsigned long)real_addr, (unsigned long)real_size, (unsigned long)user_size);
}

static void __lwe_free(void *ptr)
{
    void *real_addr;

    real_addr = __lwe_cancel_overflow((unsigned long)ptr);
    __libc_free(real_addr);
}

static void *__lwe_memalign(size_t alignment, size_t size)
{
    void *real_addr;
    void *user_addr;
    size_t real_size;
    size_t user_size = size;

    user_size = ROUNDUP(user_size, alignment);
    real_size = RESIZE(user_size);
    real_addr = __libc_malloc(real_size);
    if (real_addr == NULL) {
        return NULL;
    }

    user_addr = __lwe_setup_overflow((unsigned long)real_addr, (unsigned long)real_size, (unsigned long)user_size);
    return (void *)user_addr;
}

static void *__lwe_valloc(size_t size)
{
    return __lwe_memalign(getpagesize(), size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void *malloc(size_t size)
{
    void *ptr;

    ptr = __lwe_malloc(size);
    dd("[%s](%zu) = %p\n", __func__, size, ptr);
    return ptr;
}

void *realloc(void *old_ptr, size_t size)
{
    void *ptr;

    if (old_ptr) {
        __lwe_free(old_ptr);
    }

    ptr = __lwe_malloc(size);
    dd("[%s](%p, %zu) = %p\n", __func__, old_ptr, size, ptr);
    return ptr;
}

void *calloc(size_t nmemb, size_t size)
{
    void *ptr;

    ptr = __lwe_malloc(nmemb * size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    dd("[%s](%zu, %zu) = %p\n", __func__, nmemb, size, ptr);
    return ptr;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    void *ptr;

    ptr = __lwe_memalign(alignment, size);
    dd("[%s](%p, %zu, %zu)\n", __func__, ptr, alignment, size);
    if (ptr) {
        *memptr = ptr;
        return 0;
    }

    return -1;
}

void *memalign(size_t alignment, size_t size)
{
    void *ptr;

    ptr = __lwe_memalign(alignment, size);
    dd("[%s](%zu, %zu) = %p\n", __func__, alignment, size, ptr);
    return ptr;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    void *ptr;

    ptr = __lwe_memalign(alignment, size);
    dd("[%s](%zu, %zu) = %p\n", __func__, alignment, size, ptr);
    return ptr;
}

void *valloc(size_t size)
{
    void *ptr;

    ptr = __lwe_valloc(size);
    dd("[%s](%zu) = %p\n", __func__, size, ptr);
    return ptr;
}

void *pvalloc(size_t size)
{
    void *ptr;

    ptr = __lwe_valloc(size);
    dd("[%s](%zu) = %p\n", __func__, size, ptr);
    return ptr;
}

void free(void *ptr)
{
    if (ptr == NULL)
        return;

    dd("[%s](%p)\n", __func__, ptr);
    __lwe_free(ptr);
}
