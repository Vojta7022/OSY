/*
 * Simple page table manipulation routines
 */

#pragma once

#include "types.h"
#include "cpu.h"

class Ptab
{
    public:

	/** Page attributes */
	enum {
		PRESENT = 1<<0,
		RW      = 1<<1,
		USER    = 1<<2,
		ACCESS  = 1<<5,
		DIRTY   = 1<<6,
	};

        /** Inserts a 4 KB mapping into the page table. */
        static bool insert_mapping (mword virt, mword phys, mword attr);
        /** Returns the page table entry for the virtual address `virt`, INCLUDING THE ATTRIBUTE BITS. */
        static mword get_mapping (mword virt);

        /**
         * Maps the passed physical address to the fixed virtual address REMAP_SADDR as a single 4MB page.
         * This mapping is temporary, and is changed the next time this method is called.
         *
         * This is useful for one-off reads from a given physical address. We cannot use Kalloc::phys2virt(),
         * because the 1:1 mapping used by it may not cover the whole physical RAM.
         */
        static void* remap (mword phys);
};
