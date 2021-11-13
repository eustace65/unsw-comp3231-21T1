#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <opt-unsw.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
/* Place your page table functions here */


void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.
     *
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    // Check faulttype
    switch (faulttype) {
        case VM_FAULT_READONLY:
            return EFAULT;
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        default:
            return EINVAL;
    }
    // Index
    paddr_t paddr = KVADDR_TO_PADDR(faultaddress);
    bool lv1_allocated = false;
    bool lv2_allocated = false;
    uint32_t lv1_index = paddr >> 24;
    uint32_t lv2_index = (paddr << 8) >> 26;
    uint32_t lv3_index = (paddr << 14) >> 26;
    int dirty = 0;
    if (curproc == NULL) {
        /*
         * No process. This is probably a kernel fault early
         * in boot. Return EFAULT so as to panic instead of
         * getting into an infinite faulting loop.
         */
        return EFAULT;
    }

    struct addrspace *as = proc_getas();
    if (as == NULL) {
        /*
         * No address space set up. This is probably also a
         * kernel fault early in boot.
         */
        return EFAULT;
    }

    /* Assert that the address space has been set up properly. */
    struct as_segment *current_seg = as->as_segment;
    if (current_seg == NULL) {
        return EFAULT;
    }
    if (as->pagetable == NULL) {
        return EFAULT;
    }
    // If virtual address legal
    bool found = false;
    while (current_seg != NULL) {
        vaddr_t vbase = current_seg->vbase;
        vaddr_t vtop = vbase + current_seg->npage * PAGE_SIZE;
        if (faultaddress >= vbase && faultaddress < vtop) {
            if (current_seg->mode) {
                dirty = TLBLO_DIRTY;
            } else {
                dirty = 0;
            }
            found = true;
            break;
        }
        current_seg = current_seg->next;
    }
    // Invalid region
    if (found == false) {
        return EFAULT;
    }
    // Allocate memory for hierachical page table
    // LV1
    if (as->pagetable[lv1_index] == NULL) {
        as->pagetable[lv1_index] = kmalloc(sizeof(paddr_t *) * SECOND_LEVEL);
        lv1_allocated = true;
        if (as->pagetable[lv1_index] == NULL) {
            return ENOMEM;
        }
        for (int i = 0; i < SECOND_LEVEL; i++) {
            as->pagetable[lv1_index][i] = NULL;
        }
    }
    //LV2
    if (as->pagetable[lv1_index][lv2_index] == NULL) {
        as->pagetable[lv1_index][lv2_index] = kmalloc(sizeof(paddr_t) * THIRD_LEVEL);
        lv2_allocated = true;
        if (as->pagetable[lv1_index][lv2_index] == NULL) {
            if (lv1_allocated) {
                kfree(as->pagetable[lv1_index]);
            }
            return ENOMEM;
        }
        for (int j = 0; j < THIRD_LEVEL; j++) {
            as->pagetable[lv1_index][lv2_index][j] = 0;
        }
    }
    // New page
    if (as->pagetable[lv1_index][lv2_index][lv3_index] == 0) {
        vaddr_t v_page = alloc_kpages(1);
        bzero((void *)v_page, PAGE_SIZE);
        if (v_page == 0) {
            if (lv2_allocated) {
                kfree(as->pagetable[lv1_index][lv2_index]);
            }
            if (lv1_allocated) {
                kfree(as->pagetable[lv1_index]);
            }
            return ENOMEM;
        }
        as->pagetable[lv1_index][lv2_index][lv3_index] = (KVADDR_TO_PADDR(v_page) & PAGE_FRAME) | dirty | TLBLO_VALID;
    }
    uint32_t entryhi = faultaddress & PAGE_FRAME;
    uint32_t entrylow = as->pagetable[lv1_index][lv2_index][lv3_index];
    int spl = splhigh();
    // Randomly add pagetable entry to the TLB.
    tlb_random(entryhi, entrylow);
    splx(spl);
    return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
    panic("vm tried to do tlb shootdown?!\n");
}

