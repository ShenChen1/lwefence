#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

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

void *__libc_malloc(size_t size);
void __libc_free(void *ptr);
void *__libc_realloc(void *ptr, size_t size);
void *__libc_calloc(size_t nmemb, size_t size);
void *__libc_memalign(size_t alignment, size_t size);
void *__libc_valloc(size_t size);

static void *__reload_malloc(size_t size)
{
    return __libc_malloc(size);
}

static void __reload_free(void *ptr)
{
    __libc_free(ptr);
}

static void *__reload_realloc(void *old_ptr, size_t size)
{
    return __libc_realloc(old_ptr, size);
}

static void *__reload_calloc(size_t nmemb, size_t size)
{
    return __libc_calloc(nmemb, size);
}

static void *__reload_memalign(size_t alignment, size_t size)
{
    return __libc_memalign(alignment, size);
}

static void *__reload_valloc(size_t size)
{
    return __libc_valloc(size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LWE_ERROR(fmt, ...)                                                                      \
    do {                                                                                         \
        __lwe_printf(LWE_LOG_LV_ERR, "ERROR  (%s|%d): " fmt, __func__, __LINE__, ##__VA_ARGS__); \
    } while (0)

#define LWE_INFO(fmt, ...)                                           \
    do {                                                             \
        __lwe_printf(LWE_LOG_LV_INFO, "INFO : " fmt, ##__VA_ARGS__); \
    } while (0)

#define LWE_DEBUG(fmt, ...)                                                                      \
    do {                                                                                         \
        __lwe_printf(LWE_LOG_LV_DBG, "DEBUG  (%s|%d): " fmt, __func__, __LINE__, ##__VA_ARGS__); \
    } while (0)

#define MAGICNUM (0xdeadbeef)
#define RESIZE(size) (ROUNDUP((size), sizeof(unsigned long)) + sizeof(lwe_node) + 2 * getpagesize())

enum {
    LWE_RIGHT_OVERFLOW_MODE,
    LWE_LEFT_OVERFLOW_MODE,
};

enum {
    LWE_LOG_LV_ERR,
    LWE_LOG_LV_INFO,
    LWE_LOG_LV_DBG,
};

enum {
    LWE_OPEN_CHECK_STATUS,
    LWE_CLOSE_CHECK_STATUS,
};

typedef struct {
    unsigned long magic_value;
    unsigned long real_addr;
    unsigned long real_size;
    unsigned long align_addr;
} lwe_node;

typedef void *(*lwe_malloc_func)(size_t size);
typedef void (*lwe_free_func)(void *ptr);
typedef void *(*lwe_realloc_func)(void *old_ptr, size_t size);
typedef void *(*lwe_calloc_func)(size_t nmemb, size_t size);
typedef void *(*lwe_memalign_func)(size_t alignment, size_t size);
typedef void *(*lwe_valloc_func)(size_t size);

int g_lwe_mode   = LWE_RIGHT_OVERFLOW_MODE;
int g_lwe_status = LWE_OPEN_CHECK_STATUS;
int g_lwe_loglv  = LWE_LOG_LV_INFO;

static lwe_malloc_func s_lwe_malloc_func     = __reload_malloc;
static lwe_free_func s_lwe_free_func         = __reload_free;
static lwe_realloc_func s_lwe_realloc_func   = __reload_realloc;
static lwe_calloc_func s_lwe_calloc_func     = __reload_calloc;
static lwe_memalign_func s_lwe_memalign_func = __reload_memalign;
static lwe_valloc_func s_lwe_valloc_func     = __reload_valloc;

static int __lwe_printf(int level, const char *fmt, ...)
{
    int n = 0;
    va_list ap;

    if (level <= g_lwe_loglv) {
        va_start(ap, fmt);
        n = vprintf(fmt, ap);
        va_end(ap);
    }

    return n;
}

static void *__lwe_setup_overflow(unsigned long real_addr, unsigned long real_size, unsigned long user_size)
{
    unsigned long align_addr;
    unsigned long user_addr;
    lwe_node *pNode;

    if (g_lwe_mode == LWE_RIGHT_OVERFLOW_MODE) {
        align_addr = ROUNDDOWN(real_addr + real_size, getpagesize()) - getpagesize();
        user_addr  = align_addr - user_size;
        pNode      = (lwe_node *)ROUNDDOWN(user_addr, sizeof(unsigned long)) - 1;
    } else if (g_lwe_mode == LWE_LEFT_OVERFLOW_MODE) {
        align_addr = ROUNDUP(real_addr + sizeof(lwe_node), getpagesize());
        user_addr  = align_addr + getpagesize();
        pNode      = (lwe_node *)ROUNDDOWN(user_addr - getpagesize(), sizeof(unsigned long)) - 1;
    } else {
        assert(0);
    }

    if (mprotect((void *)align_addr, getpagesize(), PROT_NONE) < 0) {
        LWE_ERROR("%s\n", strerror(errno));
        assert(0);
    }

    LWE_DEBUG("0x%x 0x%x 0x%x 0x%x 0x%x\n", real_addr, real_size, user_addr, user_size, align_addr);

    pNode->magic_value = MAGICNUM;
    pNode->real_addr   = real_addr;
    pNode->real_size   = real_size;
    pNode->align_addr  = align_addr;

    return (void *)user_addr;
}

static void *__lwe_cancel_overflow(unsigned long user_addr)
{
    unsigned long align_addr;
    unsigned long real_addr;
    lwe_node *pNode = NULL;

    if (g_lwe_mode == LWE_RIGHT_OVERFLOW_MODE) {
        pNode = (lwe_node *)ROUNDDOWN(user_addr, sizeof(unsigned long)) - 1;
    } else if (g_lwe_mode == LWE_LEFT_OVERFLOW_MODE) {
        pNode = (lwe_node *)ROUNDDOWN(user_addr - getpagesize(), sizeof(unsigned long)) - 1;
    } else {
        assert(0);
    }

    assert(pNode->magic_value == MAGICNUM);
    align_addr = pNode->align_addr;
    real_addr  = pNode->real_addr;

    if (mprotect((void *)align_addr, getpagesize(), PROT_READ | PROT_WRITE) < 0) {
        LWE_ERROR("[%s]%s\n", __func__, strerror(errno));
        assert(0);
    }

    return (void *)real_addr;
}

static void *__lwe_malloc(size_t user_size)
{
    void *real_addr;
    size_t real_size;

    real_size = RESIZE(user_size);
    real_addr = __reload_malloc(real_size);
    if (real_addr == NULL) {
        return NULL;
    }

    return __lwe_setup_overflow((unsigned long)real_addr, (unsigned long)real_size, (unsigned long)user_size);
}

static void __lwe_free(void *ptr)
{
    void *real_addr;

    real_addr = __lwe_cancel_overflow((unsigned long)ptr);
    __reload_free(real_addr);
}

static void *__lwe_realloc(void *ptr, size_t size)
{
    if (ptr) {
        __lwe_free(ptr);
    }

    return __lwe_malloc(size);
}

static void *__lwe_calloc(size_t nelem, size_t elsize)
{
    void *real_addr;
    void *user_addr;
    size_t real_size;
    size_t user_size = nelem * elsize;

    real_size = RESIZE(user_size);
    real_addr = __reload_malloc(real_size);
    if (real_addr == NULL) {
        return NULL;
    }

    user_addr = __lwe_setup_overflow((unsigned long)real_addr, (unsigned long)real_size, (unsigned long)user_size);
    memset(user_addr, 0, user_size);

    return (void *)user_addr;
}

static void *__lwe_memalign(size_t alignment, size_t size)
{
    void *real_addr;
    void *user_addr;
    size_t real_size;
    size_t user_size = size;

    user_size = ROUNDUP(user_size, alignment);
    real_size = RESIZE(user_size);
    real_addr = __reload_malloc(real_size);
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

static void __attribute__((constructor)) __lwe_pre_init()
{
    if (g_lwe_status == LWE_OPEN_CHECK_STATUS) {
        LWE_INFO("lwefence run in %s overflow check !!!\n", g_lwe_mode == LWE_LEFT_OVERFLOW_MODE ? "left" : "right");

        s_lwe_malloc_func   = __lwe_malloc;
        s_lwe_free_func     = __lwe_free;
        s_lwe_realloc_func  = __lwe_realloc;
        s_lwe_calloc_func   = __lwe_calloc;
        s_lwe_memalign_func = __lwe_memalign;
        s_lwe_valloc_func   = __lwe_valloc;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void *malloc(size_t size)
{
    void *ptr;

    ptr = s_lwe_malloc_func(size);
    LWE_DEBUG("[%s](%d) = %p\n", __func__, size, ptr);
    return ptr;
}

void free(void *ptr)
{
    if (ptr == NULL)
        return;

    LWE_DEBUG("[%s](%p)\n", __func__, ptr);
    s_lwe_free_func(ptr);
}

void *calloc(size_t nmemb, size_t size)
{
    void *ptr;

    ptr = s_lwe_calloc_func(nmemb, size);
    LWE_DEBUG("[%s](%d, %d) = %p\n", __func__, nmemb, size, ptr);
    return ptr;
}

void *realloc(void *old_ptr, size_t size)
{
    void *ptr;

    ptr = s_lwe_realloc_func(old_ptr, size);
    LWE_DEBUG("[%s](%p, %d) = %p\n", __func__, old_ptr, size, ptr);
    return ptr;
}

void *memalign(size_t alignment, size_t size)
{
    void *ptr;

    ptr = s_lwe_memalign_func(alignment, size);
    LWE_DEBUG("[%s](%d, %d) = %p\n", __func__, alignment, size, ptr);
    return ptr;
}

void *valloc(size_t size)
{
    void *ptr;

    ptr = s_lwe_valloc_func(size);
    LWE_DEBUG("[%s](%d) = %p\n", __func__, size, ptr);
    return ptr;
}
