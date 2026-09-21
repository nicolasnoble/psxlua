/*
** Glue needed to build luac as a ps-exe. psxlua leaves the allocator to
** whoever embeds it (see luaI_realloc/luaI_free in llibc.h); an embedder with
** a heap of its own supplies these by --defsym, but luac is a standalone
** program, so it takes the kernel heap. Only luac-psx.mk compiles this file.
*/

#include <stddef.h>

#include "common/syscalls/syscalls.h"

void* memcpy(void* dst, const void* src, size_t n);

extern char __bss_end[];
extern char __sp[];

/* Room left below the stack pointer for the parser's recursion. */
#define STACK_RESERVE 0x10000

/*
** The kernel's malloc has no realloc, and psxlua's luaI_realloc does not get
** told the old size, so each block carries it. Eight bytes keeps whatever
** alignment the kernel handed back.
*/
typedef struct {
    size_t size;
    size_t reserved;
} Header;

static int heapReady;

static void initHeap(void) {
    char* base = (char*)((((unsigned)__bss_end) + 7) & ~7u);
    char* top = (char*)((unsigned)__sp - STACK_RESERVE);
    heapReady = 1;
    syscall_userInitheap(base, (size_t)(top - base));
}

void luaI_free(void* ptr) {
    if (ptr == NULL) return;
    syscall_userFree((Header*)ptr - 1);
}

void* luaI_realloc(void* ptr, size_t size) {
    Header* block;
    if (!heapReady) initHeap();
    if (size == 0) {
        luaI_free(ptr);
        return NULL;
    }
    block = (Header*)syscall_userMalloc(size + sizeof(Header));
    if (block == NULL) return NULL;
    block->size = size;
    if (ptr != NULL) {
        Header* old = (Header*)ptr - 1;
        memcpy(block + 1, ptr, old->size < size ? old->size : size);
        syscall_userFree(old);
    }
    return block + 1;
}
