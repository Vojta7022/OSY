/*
 * Simple page table manipulation routines
 */

#include "ptab.h"
#include "kalloc.h"
#include "memory.h"
#include "assert.h"

/**
 * Set a page table entry corresponding to virtual address @a virt to
 * the combination of @a phys and @a attr.
 *
 * @param[in] virt Virtual address to be mapped.
 * @param[in] phys Physical address to map at @a virt.
 * @param[in] attr Page attributes such as PRESENT, RW, USER, ACCESS, DIRTY.
 *
 * @return true in case of success, false otherwise.
 */
bool Ptab::insert_mapping (mword virt, mword phys, mword attr)
{
    // load page directory virtual address
    mword* pdir = static_cast<mword*>(Kalloc::phys2virt(Cpu::cr3()));
    mword* ptab;
    // check if the required page table already exists
    if ((pdir[virt >> 22] & PRESENT) == 0) {
        // allocate a new page table
        ptab = static_cast<mword*>(Kalloc::allocator.alloc_page(1, Kalloc::FILL_0));
        if (!ptab)
            return false;
        mword p = Kalloc::virt2phys (ptab);
        // add the page table to page directory
        pdir[virt >> 22] = p | ACCESS | RW | PRESENT | USER;
    } else {
        // get the matching page table address
        ptab = static_cast<mword*>(Kalloc::phys2virt (pdir[virt >> 22] & ~PAGE_MASK));
    }
    assert ((phys & PAGE_MASK) == 0);
    // insert the requested mapping into the page table
    ptab[(virt >> PAGE_BITS) & 0x3ff] = (phys & ~PAGE_MASK) | (attr & PAGE_MASK);
    // flush TLB to update the cached mapping
    Cpu::flush();
    return true;
}


/**
 * Get a page table entry corresponding to virtual address @a virt.
 *
 * @return The requested page table entry.
 */
mword Ptab::get_mapping (mword virt)
{
    mword* pdir = static_cast<mword*>(Kalloc::phys2virt(Cpu::cr3()));
    mword* ptab;

    if ((pdir[virt >> 22] & PRESENT) == 0)
        return 0;
    else
        ptab = static_cast<mword*>(Kalloc::phys2virt (pdir[virt >> 22] & ~PAGE_MASK));

    return ptab[(virt >> PAGE_BITS) & 0x3ff];
}

void* Ptab::remap (mword phys)
{
    mword* pdir = static_cast<mword*>(Kalloc::phys2virt(Cpu::cr3()));
    // flush TLB : old 4M mapping
    // MK: why set 0 and flush here, why not flush after setting the real value?
    pdir[REMAP_SADDR >> 22] = 0;
    Cpu::flush(REMAP_SADDR);
    // insert new mapping
    // 0xffc... masks top 10 bits, 0xe3 sets page directory entry flags (0xe3 = 0b11100011)
    pdir[REMAP_SADDR >> 22] = (phys & 0xffc00000) + 0xe3;
    // 0x3f... masks bottom 22 bits
    return reinterpret_cast<void *>(REMAP_SADDR + (phys & 0x3fffff));
}
