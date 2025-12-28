#include "mem_alloc.h"
#include <stdio.h>

/* Header for each block: 16 bytes on 32-bit:
 * 4 (size) + 4 (next) + 4 (prev) + 4 (free).
 */
typedef struct block_header
{
    unsigned long size;        // size of user data in bytes
    struct block_header *next; // next block in the list
    struct block_header *prev; // previous block in the list
    int free;                  // 1 = free, 0 = allocated
} block_header_t;

// List of all blocks in address order
static block_header_t *g_head = 0;
static block_header_t *g_tail = 0;

// For a simple range check in my_free
static void *g_heap_start = 0;

#define ALIGNMENT 8UL
#define HEADER_SIZE ((unsigned long)sizeof(block_header_t))

static inline void *nbrk(void *address);

#ifdef NOVA

/**********************************/
/* nbrk() implementation for NOVA */
/**********************************/

static inline unsigned syscall2(unsigned w0, unsigned w1)
{
    asm volatile("   mov %%esp, %%ecx    ;"
                 "   mov $1f, %%edx      ;"
                 "   sysenter            ;"
                 "1:                     ;"
                 : "+a"(w0)
                 : "S"(w1)
                 : "ecx", "edx", "memory");
    return w0;
}

static void *nbrk(void *address)
{
    return (void *)syscall2(3, (unsigned)address);
}
#else

/***********************************/
/* nbrk() implementation for Linux */
/***********************************/

#include <unistd.h>

static void *nbrk(void *address)
{
    void *current_brk = sbrk(0);
    if (address != NULL)
    {
        int ret = brk(address);
        if (ret == -1)
            return NULL;
    }
    return current_brk;
}

#endif

void *my_malloc(unsigned long size)
{
    if (size == 0)
        return 0; // No allocation for zero size

    // Remember where the heap starts (first call)
    if (!g_heap_start)
    {
        g_heap_start = nbrk(0);
        if (!g_heap_start)
            return 0;
    }

    // Align to 8 bytes
    unsigned long aligned = (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);

    block_header_t *block = 0;
    block_header_t *cur;

    // 1) First-fit search for a free block
    for (cur = g_head; cur != 0; cur = cur->next)
    {
        if (cur->free && cur->size >= aligned)
        {
            block = cur;
            break;
        }
    }

    if (block)
    {
        // Reuse existing free block, possibly split it
        unsigned long remaining = block->size - aligned;

        // Split only if leftover is big enough for a useful block
        if (remaining >= HEADER_SIZE + ALIGNMENT)
        {
            char *start = (char *)block;
            block_header_t *new_block =
                (block_header_t *)(start + HEADER_SIZE + aligned); // Pointer to the new block header

            new_block->size = remaining - HEADER_SIZE; // Size of the new free block
            new_block->free = 1;
            new_block->prev = block; 
            new_block->next = block->next;

            if (block->next)
                block->next->prev = new_block; // Update next block's previous pointer
            else
                g_tail = new_block; // set the heap's tail pointer

            block->next = new_block;
            block->size = aligned;
        }

        block->free = 0;
    }
    else
    {
        // 2) No free block -> ask kernel for more heap space    
        unsigned long total = HEADER_SIZE + aligned;

        void *old_brk = nbrk(0); // Get current program break
        if (!old_brk)
            return 0;

        void *new_brk = (void *)((char *)old_brk + total); // Calculate new program break
        void *ret = nbrk(new_brk); // Request new program break
        if (!ret)
            return 0; // allocation failed

        block = (block_header_t *)ret; // Pointer to the new block header
        block->size = aligned;
        block->free = 0;
        block->next = 0;
        block->prev = g_tail;

        if (g_tail)
            g_tail->next = block; // Update previous tail's next pointer
        else
            g_head = block; // Set head if this is the first block

        g_tail = block;
    }

    // User pointer is right after the header
    return (void *)((char *)block + HEADER_SIZE);
}

int my_free(void *address)
{
    if (!address)
        return 0;

    // Compute pointer to block header
    block_header_t *block =
        (block_header_t *)((char *)address - HEADER_SIZE);

    // Check if pointer is within our heap and is a valid block
    void *current_brk = nbrk(0);
    if (!g_heap_start || !current_brk)
        return 1;

    if ((void *)block < g_heap_start || (void *)block >= current_brk)
        return 1; // pointer not from our heap

    block_header_t *cur;
    int found = 0;
    for (cur = g_head; cur != 0; cur = cur->next)
    {
        if (cur == block)
        {
            found = 1;
            break;
        }
    }
    if (!found)
        return 1; // unknown block

    if (block->free)
        return 1; // double free

    // Mark as free
    block->free = 1;

    // 1) Merge with previous free block
    if (block->prev && block->prev->free)
    {
        block_header_t *p = block->prev;
        p->size += HEADER_SIZE + block->size;
        p->next = block->next;

        if (block->next)
            block->next->prev = p;
        else
            g_tail = p;

        block = p; // continue with merged block
    }

    // 2) Merge with next free block
    if (block->next && block->next->free)
    {
        block_header_t *n = block->next;
        block->size += HEADER_SIZE + n->size;
        block->next = n->next;

        if (n->next)
            n->next->prev = block;
        else
            g_tail = block;
    }

    return 0;
}
