/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

#include <elf.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
    struct addrspace *as;

    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }

    /*
     * Initialize as needed.
     */

    // set the list of segments as null
    as->as_segment = NULL;
    // allocate and initialize page table
    as->pagetable = (paddr_t ***)alloc_kpages(1);
    if (as->pagetable == NULL) {
        kfree(as);
        return NULL;
    }
    // first_level 256
    for(int i = 0; i < FIRST_LEVEL; i++){
        as->pagetable[i] = NULL;
    }

    return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    struct addrspace *newas;

    newas = as_create();
    if (newas==NULL) {
        return ENOMEM;
    }

    /*
     * Write this.
     */
    // Copy pages from old address to new address
    for (int lv1_index = 0; lv1_index < FIRST_LEVEL; lv1_index++) {
        if (old->pagetable[lv1_index] == NULL)
            continue;
        // LV1 exists
        newas->pagetable[lv1_index] = kmalloc(SECOND_LEVEL * sizeof(paddr_t *));
        if (newas->pagetable[lv1_index] == NULL) {
            return ENOMEM;
        }
        for (int i = 0; i < SECOND_LEVEL; i++) {
            newas->pagetable[lv1_index][i] = NULL;
        }
        for (int lv2_index = 0; lv2_index < SECOND_LEVEL; lv2_index++) {
            if (old->pagetable[lv1_index][lv2_index] == NULL)
                continue;
            // LV2 exists
            newas->pagetable[lv1_index][lv2_index] = kmalloc(THIRD_LEVEL * sizeof(paddr_t));
            if (newas->pagetable[lv1_index][lv2_index] == NULL) {
                return ENOMEM;
            }
            for (int lv3_index = 0; lv3_index < THIRD_LEVEL; lv3_index++) {
                if (old->pagetable[lv1_index][lv2_index][lv3_index] == 0) {
                    newas->pagetable[lv1_index][lv2_index][lv3_index] = 0;
                    continue;
                }
                // LV3 old page exists
                int dirty = old->pagetable[lv1_index][lv2_index][lv3_index] & TLBLO_DIRTY;
                vaddr_t v_page = alloc_kpages(1);
                bzero((void *)v_page, PAGE_SIZE);
                if (v_page == 0) {
                    return ENOMEM;
                }
                vaddr_t old_v_page = PADDR_TO_KVADDR(
                    old->pagetable[lv1_index][lv2_index][lv3_index] & PAGE_FRAME);
                // copy old page to new page address
                memmove((void *)v_page, (const void *)old_v_page, PAGE_SIZE);
                newas->pagetable[lv1_index][lv2_index][lv3_index] =
                    (KVADDR_TO_PADDR(v_page) & PAGE_FRAME) | dirty | TLBLO_VALID;
            }
        }
    }
    // Copy as_segment from old address to new address
    struct as_segment *old_seg = old->as_segment;
    struct as_segment *new_seg = newas->as_segment;
    while (old_seg != NULL) {
        struct as_segment *segment = kmalloc(sizeof(struct as_segment));
        if (segment == NULL) {
            as_destroy(newas);
            return ENOMEM;
        }
        // copy data
        segment->vbase = old_seg->vbase;
        segment->npage = old_seg->npage;
        segment->mode = old_seg->mode;
        segment->prev_mode = old_seg->prev_mode;
        segment->next = NULL;
        if (new_seg == NULL) {
            // First node
            newas->as_segment = segment;
        } else {
            new_seg->next = segment;
        }
        old_seg = old_seg->next;
        // keep new_seg the last node in the list
        new_seg = segment;
    }
    *ret = newas;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
    /*
     * Clean up as needed.
     */
    int i, j, k;

    if(as == NULL) {
        return;
    }
    //clean the page table
    for (i = 0; i < FIRST_LEVEL; i++) {
        if (as->pagetable[i] != NULL) {
            //level 2
            for (j = 0; j < SECOND_LEVEL; j++) {
                if (as->pagetable[i][j] != NULL) {
                    //level 3
                    for (k = 0; k < THIRD_LEVEL; k++) {
                        if (as->pagetable[i][j][k] != 0) {
                            free_kpages(PADDR_TO_KVADDR(as->pagetable[i][j][k] & PAGE_FRAME));
                        }
                    }
                    kfree(as->pagetable[i][j]);
                }
            }
            kfree(as->pagetable[i]);
        }
    }
    kfree(as->pagetable);

    // free segement
    struct as_segment *curr = as->as_segment;
    struct as_segment *next;
	while (curr != NULL) {
		next = curr->next;
		kfree(curr);
		curr = next;
	}

    kfree(as);
}

// as_activate() and as_deactivate() can be copied from dumbvm.
void
as_activate(void)
{
    int i, spl;
    struct addrspace *as;

    as = proc_getas();
    if (as == NULL) {
        return;
    }

    /* Disable interrupts on this CPU while frobbing the TLB. */
    spl = splhigh();

    for (i=0; i<NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }

    splx(spl);
}

// as_activate() and as_deactivate() can be copied from dumbvm.
void
as_deactivate(void)
{
    as_activate();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
         int readable, int writeable, int executable)
{
    /*
     * Write this.
     */

    if(as == NULL){
        return EFAULT;
    }

    //idea from dumbvm.c
    memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr &= PAGE_FRAME;
    memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

    size_t npage = memsize / PAGE_SIZE;

    struct as_segment *segment = kmalloc(sizeof(struct as_segment));

    if (segment == NULL) {
        return ENOMEM;
    }

    segment->vbase = vaddr;
    segment->npage = npage;
    segment->mode = writeable;
    segment->prev_mode = segment->mode;
    segment->next = NULL;
    //add region to addrsapce
    if (as->as_segment != NULL) {
        // Add to end of linked list
        struct as_segment *curr;
        for (curr = as->as_segment; curr->next != NULL; curr = curr->next) {
        }
        curr->next = segment;
    } else {
        // Add to start
        as->as_segment = segment;
    }

    (void)readable;
    (void)executable;
    return 0;
}

int
as_prepare_load(struct addrspace *as)
{
    /*
     * Write this.
     */
    // Change mode to writable
    struct as_segment *current_seg = as->as_segment;
    while (current_seg != NULL) {
        current_seg->mode = 1;
        current_seg = current_seg->next;
    }
    return 0;
}

int
as_complete_load(struct addrspace *as)
{
    /*
     * Write this.
     */
    // Restore mode
    struct as_segment *current_seg = as->as_segment;
    while (current_seg != NULL) {
        current_seg->mode = current_seg->prev_mode;
        current_seg = current_seg->next;
    }
    // Flush TLB
    int spl = splhigh();
    for (int i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);
    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    /* Initial user-level stack pointer */
    *stackptr = USERSTACK;
    int result = as_define_region(as, USERSTACK - STACK_PAGES * PAGE_SIZE, STACK_PAGES * PAGE_SIZE, 1, 1, 1);
	if (result) {
		return result;
	}
    return 0;
}

