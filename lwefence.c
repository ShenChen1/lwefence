#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

enum
{
	RIGHT_OVERFLOW_CHECK,
	LEFT_OVERFLOW_CHECK,
};

typedef struct
{
	unsigned long	magic_value;
	unsigned long	real_addr;
	unsigned long	real_size;
	unsigned long 	align_addr;
} lwefence_node;

#define MAGICNUM			(0xdeadbeef)
#define RESIZE(size)		((size) + sizeof(lwefence_node) + 2*getpagesize())

#define ROUNDDOWN(a, n)						\
({								\
	unsigned long __a = (unsigned long)(a);				\
	(typeof(a))(__a - __a % (n));				\
})
// Round up to the nearest multiple of n
#define ROUNDUP(a, n)						\
({								\
	unsigned long __n = (unsigned long)(n);				\
	(typeof(a))(ROUNDDOWN((unsigned long)(a) + __n - 1, __n));	\
})

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void *__libc_malloc(size_t size);
void __libc_free(void *ptr);
void *__libc_realloc(void *ptr, size_t size);
void *__libc_calloc(size_t nmemb, size_t size);
void *__libc_memalign(size_t alignment, size_t size);
void *__libc_valloc(size_t size);

void *__reload_malloc(size_t size)
{
	return __libc_malloc(size);
}

void __reload_free(void *ptr)
{
	__libc_free(ptr);
}

void *__reload_realloc(void *old_ptr, size_t size)
{
	return __libc_realloc(old_ptr, size);
}

void *__reload_calloc(size_t nmemb, size_t size)
{
	return __libc_calloc(nmemb, size);
}

void *__reload_memalign(size_t alignment, size_t size)
{
	return __libc_memalign(alignment, size);
}

void *__reload_valloc(size_t size)
{
	return __libc_valloc(size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int g_lwefence_check_mode = RIGHT_OVERFLOW_CHECK;
int g_lwefence_check_start = 1;


void __attribute__((constructor)) preinit()
{
	printf("lwefence run in %s overflow check !!!\n", g_lwefence_check_mode ? "left" : "right");
}

static void *__setup_overflow(unsigned long real_addr, unsigned long real_size, unsigned long user_size)
{
	unsigned long align_addr;
	unsigned long user_addr;
	lwefence_node *pNode;
	
	if (g_lwefence_check_mode == RIGHT_OVERFLOW_CHECK)
	{
		align_addr = ROUNDDOWN(real_addr + real_size, getpagesize()) - getpagesize();
		user_addr = align_addr - user_size;
		pNode = (lwefence_node *)user_addr - 1;
	}
	else if (g_lwefence_check_mode == LEFT_OVERFLOW_CHECK)
	{
		align_addr = ROUNDUP(real_addr + sizeof(lwefence_node), getpagesize());
		user_addr = align_addr + getpagesize();
		pNode = (lwefence_node *)(user_addr - getpagesize()) - 1;
	}
	else
	{
		assert(0);
	}
	
	if (mprotect((void *)align_addr, getpagesize(), PROT_NONE) < 0)
	{
		printf("%s\n", strerror(errno));
		assert(0);
	}

	//printf("0x%x 0x%x 0x%x 0x%x 0x%x\n", real_addr, real_size, user_addr, user_size, align_addr);

	pNode->magic_value = MAGICNUM;
	pNode->real_addr = real_addr;
	pNode->real_size = real_size;
	pNode->align_addr = align_addr;
	
	return (void *)user_addr;
}

static void *__cancel_overflow(unsigned long user_addr)
{
	unsigned long align_addr;
	unsigned long real_addr;
	lwefence_node *pNode = NULL;

	if (g_lwefence_check_mode == RIGHT_OVERFLOW_CHECK)
	{
		pNode = (lwefence_node *)user_addr - 1;
	}
	else if (g_lwefence_check_mode == LEFT_OVERFLOW_CHECK)
	{
		pNode = (lwefence_node *)(user_addr - getpagesize()) - 1;
	}
	else
	{
		assert(0);
	}

	assert(pNode->magic_value == MAGICNUM);
	align_addr = pNode->align_addr;
	real_addr = pNode->real_addr;
	
	if (mprotect((void *)align_addr, getpagesize(), PROT_READ|PROT_WRITE) < 0)
	{
		printf("[%s]%s\n", __func__, strerror(errno));
		assert(0);
	}
	
	return (void *)real_addr;
}

void *lwefence_malloc(size_t user_size)
{
	void *real_addr;
	size_t real_size;
	
	real_size = RESIZE(user_size);
	real_addr = __reload_malloc(real_size);
	if (real_addr == NULL)
	{
		return NULL;
	}
	
	return __setup_overflow((unsigned long)real_addr, (unsigned long)real_size, (unsigned long)user_size);
}

void lwefence_free(void *ptr)
{
	void *real_addr;

	real_addr = __cancel_overflow((unsigned long)ptr);
	__reload_free(real_addr);
}

void *lwefence_realloc(void *ptr, size_t size)
{
	lwefence_free(ptr);
	return lwefence_malloc(size);
}

void *lwefence_calloc(size_t nelem, size_t elsize)
{
	void *real_addr;
	void *user_addr;
	size_t real_size;
	size_t user_size = nelem * elsize;
	
	real_size = RESIZE(user_size);
	real_addr = __reload_malloc(real_size);
	if (real_addr == NULL)
	{
		return NULL;
	}
	
	user_addr = __setup_overflow((unsigned long)real_addr, (unsigned long)real_size, (unsigned long)user_size);
	memset(user_addr, 0, user_size);
	
	return (void *)user_addr;
}

void *lwefence_memalign(size_t alignment, size_t size)
{
	return NULL;
}

void *lwefence_valloc(size_t size)
{
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void *malloc(size_t size)
{
	void *ptr;
	
	if (g_lwefence_check_start)
	{
		ptr = lwefence_malloc(size);
	}
	else
	{
		ptr = __reload_malloc(size);
	}
	//printf("[%s](%d) = %p\n", __func__, size, ptr);
	return ptr;
}

void free(void *ptr)
{
	if (ptr == NULL)
		return;

	//printf("[%s](%p)\n", __func__, ptr);
	if (g_lwefence_check_start)
	{
		lwefence_free(ptr);
	}
	else
	{
		__reload_free(ptr);
	}
}

void *calloc(size_t nmemb, size_t size)
{
	void *ptr;
	
	if (g_lwefence_check_start)
	{
		ptr = lwefence_calloc(nmemb, size);
	}
	else
	{
		ptr = __reload_calloc(nmemb, size);
	}
	//printf("[%s](%d, %d) = %p\n", __func__, nmemb, size, ptr);
	return ptr;
}

void *realloc(void *old_ptr, size_t size)
{
	void *ptr;

	if (g_lwefence_check_start)
	{
		ptr = lwefence_realloc(old_ptr, size);
	}
	else
	{
		ptr = __reload_realloc(old_ptr, size);
	}
	//printf("[%s](%p, %d) = %p\n", __func__, old_ptr, size, ptr);
	return ptr;
}

void *memalign(size_t alignment, size_t size)
{
	void *ptr;

	if (g_lwefence_check_start)
	{
		ptr = lwefence_memalign(alignment, size);
	}
	else
	{
		ptr = __reload_memalign(alignment, size);
	}
	//printf("[%s](%d, %d) = %p\n", __func__, alignment, size, ptr);
	return ptr;
}

void *valloc(size_t size)
{
	void *ptr;

	if (g_lwefence_check_start)
	{
		ptr = lwefence_valloc(size);
	}
	else
	{
		ptr = __reload_valloc(size);
	}
	//printf("[%s](%d) = %p\n", __func__, size, ptr);
	return ptr;
}
