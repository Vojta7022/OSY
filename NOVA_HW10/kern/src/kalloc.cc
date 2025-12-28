/*
 * Very simple memory allocator
 */

#include "kalloc.h"
#include "memory.h"
#include "stdio.h"
#include "extern.h"
#include "string.h"

// Memory pool boundaries defined in the linker script
extern char _mempool_f, _mempool_e;

// Define the allocator and initialize it (calls the constructor)
Kalloc Kalloc::allocator (reinterpret_cast<mword>(&_mempool_f),
                          reinterpret_cast<mword>(&_mempool_e));

void * Kalloc::alloc (unsigned size)
{
    if (end - size < begin)
        panic ("kernel : no mem\n");

    end -= size;
    return reinterpret_cast<void*>(end);
}

bool Kalloc::is_page_allocated(unsigned idx)
{
    return page_bitmap[idx >> 5] & (1 << (idx & 0x1f));
}

void Kalloc::mark_page_allocated(unsigned idx, bool allocated)
{
    if (allocated)
        page_bitmap[idx >> 5] |= (1 << (idx & 0x1f));
    else
        page_bitmap[idx >> 5] &= ~(1 << (idx & 0x1f));
}

/**
 * Allocate @a count virtually contiguous pages and optionally fill
 * them with 0x00 or 0xFF.
 *
 * @return Virtual address of the first allocated page
 */
void * Kalloc::alloc_page (unsigned count, Fill fill)
{
    unsigned found = 0, first = 0;

    for (unsigned i = 0; i < ((end - begin) >> PAGE_BITS) && found < count; i++) {
        if (page_bitmap[i >> 5] == 0xffffffff) {
            i += 31;
            found = 0;
            continue;
        }
        if (!is_page_allocated(i)) {
            if (found++ == 0)
                first = i;
        } else {
            found = 0;
        }
    }

    if (found != count)
        return NULL;

    for (unsigned i = 0; i < count; i++)
        mark_page_allocated(first + i, true);

    void * p = reinterpret_cast<void *>(begin + first * PAGE_SIZE);
    if (fill)
        memset (p, fill == FILL_0 ? 0 : ~0, count * PAGE_SIZE);

    return p;
}

/**
 * Free a page previously allocated with Kalloc::alloc_page().
 *
 * @param[in] virt Virtual address of the page to free.
 */
void Kalloc::free_page (void *virt)
{
    mword p = reinterpret_cast<mword>(virt);
    if (p < begin || p >= end || (p & (PAGE_SIZE - 1)) != 0 ||
        !is_page_allocated((p - begin) >> PAGE_BITS))
        panic ("kernel: attempt to free invalid page\n");

    mark_page_allocated((p - begin) >> PAGE_BITS, false);
}

void * Kalloc::phys2virt (mword phys)
{
    mword virt = phys + reinterpret_cast<mword>(&OFFSET);
    return reinterpret_cast<void*>(virt);
}

mword Kalloc::virt2phys (void * virt)
{
    mword phys = reinterpret_cast<mword>(virt) - reinterpret_cast<mword>(&OFFSET);
    return phys;
}
