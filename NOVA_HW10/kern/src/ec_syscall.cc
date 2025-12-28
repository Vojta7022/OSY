#include "ec.h"
#include "ptab.h"
#include "kalloc.h"

// Simple alignment helper for pages
static inline mword align_up_local(mword x, mword a)
{
    return (x + a - 1) & ~(a - 1);
}

// Simple zeroing helper (local replacement for memset)
static inline void zero_range(mword addr, mword size)
{
    unsigned char *p = reinterpret_cast<unsigned char *>(addr);
    for (mword i = 0; i < size; ++i)
        p[i] = 0;
}

typedef enum
{
    sys_print = 1,
    sys_sum = 2,
    sys_nbrk = 3,
    sys_thr_create = 4,
    sys_thr_yield = 5,
} Syscall_numbers;

/**
 * System call handler
 *
 * @param[in] a Value of the `al` register before the system call
 */
void Ec::syscall_handler(uint8 a)
{
    // Get access to registers stored during entering the system - see
    // entry_sysenter in entry.S
    Sys_regs *r = current->sys_regs();
    Syscall_numbers number = static_cast<Syscall_numbers>(a);

    switch (number)
    {
    case sys_print:
    {
        // Tisk řetězce na sériovou linku
        char *data = reinterpret_cast<char *>(r->esi);
        unsigned len = r->edi;
        for (unsigned i = 0; i < len; i++)
            printf("%c", data[i]);
        break;
    }
    case sys_sum:
    {
        // Naprosto nepotřebné systémové volání na sečtení dvou čísel
        int first_number = r->esi;
        int second_number = r->edi;
        r->eax = first_number + second_number;
        break;
    }
    case sys_nbrk:
    {
        const mword USER_BREAK_MAX = 0xbffff000;

        mword old_brk = Ec::break_current;
        mword new_brk = r->esi; // address argument from user

        // address == NULL → just query current break
        if (!new_brk)
        {
            r->eax = old_brk;
            break;
        }

        // range check
        if (new_brk < Ec::break_min || new_brk > USER_BREAK_MAX)
        {
            r->eax = 0;
            break;
        }

        // no change
        if (new_brk == old_brk)
        {
            r->eax = old_brk;
            break;
        }

        bool failed = false;
        mword v = 0;

        if (new_brk > old_brk)
        {
            // grow heap: map new pages in [old_up, new_up)
            mword old_up = align_up_local(old_brk, PAGE_SIZE); // align up to page size
            mword new_up = align_up_local(new_brk, PAGE_SIZE);

            for (v = old_up; v < new_up; v += PAGE_SIZE)
            {
                // allocate one zero-filled physical page
                void *page = Kalloc::allocator.alloc_page(1, Kalloc::FILL_0);
                if (!page)
                {
                    failed = true;
                    break;
                }

                mword phys = Kalloc::virt2phys(page); // convert kernel virtual address to physical address
                // map page into user space
                if (!Ptab::insert_mapping(v, phys,
                                          Ptab::PRESENT | Ptab::USER | Ptab::RW)) // apply user+rw+present permissions
                {
                    Kalloc::allocator.free_page(page);
                    failed = true;
                    break;
                }
            }

            if (failed)
            {
                // rollback mapped pages in [old_up, v)
                mword rollback_up = align_up_local(old_brk, PAGE_SIZE);
                for (mword u = rollback_up; u < v; u += PAGE_SIZE)
                {
                    mword entry = Ptab::get_mapping(u); // get page table entry
                    if (!entry)
                        continue;

                    mword phys = entry & ~PAGE_MASK;
                    void *kvirt = Kalloc::phys2virt(phys); // convert physical address to kernel virtual address
                    Kalloc::allocator.free_page(kvirt);
                    Ptab::insert_mapping(u, 0, 0); // unmap
                }
                r->eax = 0;
                break;
            }

            // zero newly accessible heap region [old_brk, new_brk)
            zero_range(old_brk, new_brk - old_brk);
        }
        else
        {
            // shrink heap: unmap pages in [new_up, old_up)
            mword new_up = align_up_local(new_brk, PAGE_SIZE);
            mword old_up = align_up_local(old_brk, PAGE_SIZE);

            for (v = new_up; v < old_up; v += PAGE_SIZE)
            {
                mword entry = Ptab::get_mapping(v);
                if (!entry)
                    continue;

                mword phys = entry & ~PAGE_MASK;
                void *kvirt = Kalloc::phys2virt(phys);
                Kalloc::allocator.free_page(kvirt);
                Ptab::insert_mapping(v, 0, 0); // unmap
            }
        }

        Ec::break_current = new_brk;
        r->eax = old_brk;
        break;
    }

    default:
        printf("unknown syscall %d\n", number);
        break;
    };

    ret_user_sysexit();
}
