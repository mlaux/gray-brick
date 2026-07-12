#include <Memory.h>
#include <stddef.h>

#include "arena.h"

// 256 KB reserved for other allocations
// not sure if i need this much, 
#define ARENA_SAFETY_MARGIN 262144

static unsigned char *arena_base;
static unsigned char *arena_ptr;
static unsigned char *arena_end;

int arena_init(void)
{
    Size grow_bytes;
    Size available;

    // this both compacts the heap and find largest contiguous block
    available = MaxMem(&grow_bytes);

    // Leave safety margin for other allocations
    if (available <= ARENA_SAFETY_MARGIN) {
        return 0;
    }
    available -= ARENA_SAFETY_MARGIN;

    arena_base = (unsigned char *) NewPtr(available);
    if (!arena_base) {
        return 0;
    }

    arena_ptr = arena_base;
    arena_end = arena_base + available;
    return 1;
}

void *arena_alloc(size_t size)
{
    unsigned char *p;

    // align to 4 bytes for 68k, could probably be 2?
    size = (size + 3) & ~3;

    if (arena_ptr + size > arena_end) {
        return NULL;
    }

    p = arena_ptr;
    arena_ptr += size;
    return p;
}

// give back the tail of the most recent allocation. no-op if something
// else was allocated after it (e.g. a bank cache page mid-compile)
void arena_shrink(void *ptr, size_t old_size, size_t new_size)
{
    old_size = (old_size + 3) & ~3;
    new_size = (new_size + 3) & ~3;

    if ((unsigned char *) ptr + old_size != arena_ptr) {
        return;
    }
    if (new_size >= old_size) {
        return;
    }

    arena_ptr = (unsigned char *) ptr + new_size;
}

void arena_reset(void)
{
    arena_ptr = arena_base;
}

size_t arena_remaining(void)
{
    return arena_end - arena_ptr;
}

size_t arena_size(void)
{
    return arena_end - arena_base;
}

void arena_destroy(void)
{
    if (arena_base) {
        DisposePtr(arena_base);
        arena_base = NULL;
        arena_ptr = NULL;
        arena_end = 0;
    }
}