/*
 * Very simple memory allocator
 */

#pragma once

#include "types.h"
#include "memory.h"

class Kalloc
{
    private:
        const mword begin;
        mword end;

        // Bitmap for 64 MB of memory
        static const unsigned mempool_pages = 64*(1<<20)/PAGE_SIZE;
        unsigned page_bitmap[mempool_pages/32];

        bool is_page_allocated(unsigned idx);
        void mark_page_allocated(unsigned idx, bool allocated);

    public:

        enum Fill
        {
            NOFILL = 0,
            FILL_0,
            FILL_1
        };

        static Kalloc allocator;

        Kalloc (mword virt_begin, mword virt_end) : begin (virt_begin), end (virt_end) {}

        void * alloc(unsigned size);

        void * alloc_page (unsigned count, Fill fill = NOFILL);
        void free_page (void *);

        /**
         * Returns the virtual address that can be used to access memory with physical address `phys`.
         *
         * Even kernel can only access virtual memory, it cannot access physical memory directly.
         * However, CPU and bootloader work with physical addresses, so we sometimes need to access
         * memory via its physical addresses.
         *
         * To resolve this, part of the physical memory is mapped 1:1 to a sequential block of virtual
         * memory addresses at a known offset `OFFSET`, so that the physical address X is accessible
         * at the virtual address OFFSET + X. This mapping is configured during kernel startup at
         * `kern/src/start.S` (section `SETUP BOOT PAGE TABLE`).
         */
        static void * phys2virt (mword);

        /** Return the physical address that is mapped from the virtual address `virt` (opposite of `phys2virt`). */
        static mword virt2phys (void *);
};
