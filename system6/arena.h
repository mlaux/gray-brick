#ifndef _ARENA_H
#define _ARENA_H

#include <stddef.h>

// initialize arena by allocating the largest available contiguous block
// minus a safety margin, returns 1 on success and 0 on failure
int arena_init(void);

// bump-allocate from the arena, returns NULL if no space
void *arena_alloc(size_t size);

// shrink the most recent allocation to new_size, no-op if anything was
// allocated after it
void arena_shrink(void *ptr, size_t old_size, size_t new_size);

// reset arena pointer to base for instant "free all"
void arena_reset(void);

// return bytes remaining in arena
size_t arena_remaining(void);

// return total size of arena
size_t arena_size(void);

void arena_destroy(void);

#endif
