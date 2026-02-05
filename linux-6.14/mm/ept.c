/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mmap_lock.h>
#include <linux/uaccess.h>
#include "internal.h"
#include <asm/tlbflush.h>

#ifndef my_zero_pfn
#define my_zero_pfn(address) page_to_pfn(ZERO_PAGE(0))
#endif

static vm_fault_t ept_fault(struct vm_fault *vmf) {
    printk(KERN_INFO "EPT: ept_fault ENTER\n");
    struct mm_struct *mm = vmf->vma->vm_mm;

    unsigned long uaddr = vmf->address & PAGE_MASK;

    /* ποια 4KB ept-page έγινε fault μέσα στο mapping; */
    unsigned long page_no = (uaddr - vmf->vma->vm_start) >> PAGE_SHIFT;

    /* κάθε ept page έχει 512 entries (4096/8) */
    unsigned long first_entry_index = page_no << 9;   // *512

    /* το VA του process που αντιστοιχεί στο πρώτο entry αυτής της ept-page */
    unsigned long target_va = first_entry_index << PAGE_SHIFT;

    pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte;
    unsigned long pfn_value;

    printk(KERN_INFO "EPT fault: page_no=%lu first_entry=%lu target_va=0x%lx fault_addr=0x%lx\n",
       page_no, first_entry_index, target_va, vmf->address);
    
    // Άμυνα: Αν το target_va είναι 0 ή εκτός user space, return zero page
    if (target_va >= TASK_SIZE) {
        printk(KERN_INFO "EPT: target_va >= TASK_SIZE\n");
        goto zero_page;
    }
    
    // Page table walk
    pgd = pgd_offset(mm, target_va);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        printk(KERN_INFO "EPT: pgd none/bad\n");
        goto zero_page;
    }
    
    p4d = p4d_offset(pgd, target_va);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk(KERN_INFO "EPT: p4d none/bad\n");
        goto zero_page;
    }
    
    pud = pud_offset(p4d, target_va);
    if (pud_none(*pud) || pud_bad(*pud)) {
        printk(KERN_INFO "EPT: pud none/bad\n");
        goto zero_page;
    }
    
    pmd = pmd_offset(pud, target_va);
    if (pmd_none(*pmd) || pmd_bad(*pmd) || pmd_leaf(*pmd)) {
        printk(KERN_INFO "EPT: pmd none/bad\n");
        goto zero_page;
    }
    
    pfn_value = page_to_pfn(pmd_page(*pmd));
    
    printk(KERN_INFO "EPT: found pte_page_pfn=0x%lx\n", pfn_value);
    return vmf_insert_pfn(vmf->vma, vmf->address, pfn_value);

zero_page:
    printk(KERN_INFO "EPT: returning zero page\n");
    return vmf_insert_pfn(vmf->vma, vmf->address, my_zero_pfn(vmf->address));
}

static void ept_vma_close(struct vm_area_struct *vma) {
    struct mm_struct *mm = vma->vm_mm;
    
    spin_lock(&mm->ept_lock);
    if (mm->ept_vma == vma) {
        mm->ept_vma = NULL;
        mm->ept_user_addr = NULL;
    }
    spin_unlock(&mm->ept_lock);
}

static const struct vm_operations_struct ept_vm_ops = {
    .fault = ept_fault,
    .close = ept_vma_close,
};

static int ept_mmap(struct file *file, struct vm_area_struct *vma) {
    struct mm_struct *mm = current->mm;
    
    printk(KERN_INFO "EPT mmap: start=0x%lx, end=0x%lx, size=%luMB, flags=0x%lx\n",
           vma->vm_start, vma->vm_end, 
           (vma->vm_end - vma->vm_start) / (1024*1024),
           vma->vm_flags);
    
    if (!capable(CAP_SYS_ADMIN)) {
        printk(KERN_INFO "EPT mmap: permission denied (not root)\n");
        return -EPERM;
    }
    
    spin_lock(&mm->ept_lock);
    if (mm->ept_vma) {
        spin_unlock(&mm->ept_lock);
        printk(KERN_INFO "EPT mmap: already mapped\n");
        return -EBUSY;
    }
    mm->ept_vma = vma;
    mm->ept_user_addr = (unsigned long __user *)vma->vm_start;
    spin_unlock(&mm->ept_lock);
    
    // Χρήση σωστών functions για flags
    vm_flags_set(vma, VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
    vm_flags_clear(vma, VM_MAYWRITE);  // Αντί για vma->vm_flags &= ~VM_MAYWRITE
    
    // Ορισμός vm_ops
    vma->vm_ops = &ept_vm_ops;
    
    printk(KERN_INFO "EPT mmap: SUCCESS, vm_ops=%p\n", vma->vm_ops);
    return 0;
}

static const struct file_operations ept_fops = {
    .owner = THIS_MODULE,
    .mmap = ept_mmap,
};

static struct miscdevice ept_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "ept",
    .fops = &ept_fops,
    .mode = 0600,
};

void ept_sync_address(struct mm_struct *mm, unsigned long address) {
    struct vm_area_struct *vma;
    
    spin_lock(&mm->ept_lock);
    vma = mm->ept_vma;
    if (!vma) {
        spin_unlock(&mm->ept_lock);
        return;
    }
    
    // address: η διεύθυνση του χρήστη (π.χ. της main)
    // vpn: το offset μέσα στο δικό μας ept mapping
    unsigned long vpn = address >> PAGE_SHIFT;
    unsigned long ept_fault_addr = vma->vm_start + (vpn * sizeof(unsigned long));

    // Αντί για put_user, καθαρίζουμε το PTE του exposed mapping
    // Έτσι την επόμενη φορά που ο χρήστης θα διαβάσει το ept[vpn], θα ξαναμπει στην ept_fault
    zap_vma_ptes(vma, ept_fault_addr, PAGE_SIZE);
    
    spin_unlock(&mm->ept_lock);
    
    // Flush TLB για να σιγουρευτούμε ότι η αλλαγή φαίνεται αμέσως
    flush_tlb_mm_range(mm, address, address + PAGE_SIZE, PAGE_SHIFT, false);
}
EXPORT_SYMBOL(ept_sync_address);

static int __init ept_init(void) {
    int ret;
    printk(KERN_INFO "EPT: Initializing module\n");
    
    ret = misc_register(&ept_dev);
    if (ret) {
        printk(KERN_ERR "EPT: Failed to register device, error=%d\n", ret);
        return ret;
    }
    
    printk(KERN_INFO "EPT: Module loaded successfully, device: /dev/ept\n");
    return 0;
}

static void __exit ept_exit(void) {
    misc_deregister(&ept_dev);
    printk(KERN_INFO "EPT module unloaded\n");
}

module_init(ept_init);
module_exit(ept_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Exposed Page Tables for POSIX processes");
MODULE_AUTHOR("Student");