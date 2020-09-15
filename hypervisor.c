#include "hypervisor.h"

MODULE_AUTHOR("Qubasa Corp.");
MODULE_LICENSE("GPL v2");

enum SVM_SUPPORT {
  SVM_ALLOWED,
  SVM_NOT_AVAIL,
  SVM_DISABLED_AT_BIOS_NOT_UNLOCKABLE,
  SVM_DISABLED_WITH_KEY
};

bool hasMsrSupport(void) {
  uint32_t cpuid_response;

  // Get CPUID for MSR support
  __asm__("mov rax, 0x00000001" ::: "rax");
  __asm__("cpuid");
  __asm__("mov %0, edx" : "=r"(cpuid_response));

  if (cpuid_response & (1 << 5)) {
    return true;
  }
  return false;
}

bool isSvmDisabled_VM_CR(void) {
  uint32_t vm_cr;
  uint32_t high;

  // Read VM_CR MSR
  readMSR(VM_CR_ADDR, &high, &vm_cr);

  printk(KERN_INFO "Is SVM Lock enabled: %s\n",
         vm_cr & (1 << 3) ? "true" : "false");

  return (bool)(vm_cr & (1 << 4));
}

enum SVM_SUPPORT checkSvmSupport(void) {
  uint32_t cpuid_response;

  // Get CPUID for svm support
  __asm__("mov rax, 0x80000001" ::: "rax");
  __asm__("cpuid");
  __asm__("mov %0, ecx" : "=r"(cpuid_response));

  // Has SVM extension?
  if (!(cpuid_response & 0x2)) {
    return SVM_NOT_AVAIL;
  }

  if (!isSvmDisabled_VM_CR()) {
    return SVM_ALLOWED;
  }

  // Get CPUID for disabled svm at bios
  __asm__("mov rax, 0x8000000A" ::: "rax");
  __asm__("cpuid");
  __asm__("mov %0, edx" : "=r"(cpuid_response));

  // Check if SVM is disabled in BIOS
  if ((cpuid_response & 0x2) == 0) {
    return SVM_DISABLED_AT_BIOS_NOT_UNLOCKABLE;
  } else {
    return SVM_DISABLED_WITH_KEY;
  }
}

void inline enableSVM_EFER(void) {
  uint64_t cr0;
  uint64_t cs;
  uint32_t efer;
  uint32_t high;

  // Check CPL0
  // Processor must be in protected mode

  // Read VM_CR MSR
  readMSR(EFER_ADDR, &high, &efer);
  printk(KERN_INFO "Is EFER.SVM enabled: %s\n",
         efer & MY_EFER_SVME ? "true" : "false");

  // Check if processor in protected mode
  __asm__("mov %0, cr0" : "=r"(cr0));
  printk(KERN_INFO "Is protected mode enabled: %s\n",
         cr0 & 1 ? "true" : "false");

  // Read the current CPL level
  __asm__("mov %0, cs" : "=r"(cs));
  printk(KERN_INFO "DPL is: %lld\n", cs & ((1 << 13) | (1 << 14)));

  // Enable EFER.SVM
  printk(KERN_INFO "Write 1 to enable EFER.SVM\n");
  efer |= MY_EFER_SVME;
  writeMSR(EFER_ADDR, high, efer);

  readMSR(EFER_ADDR, &high, &efer);
  printk(KERN_INFO "Is EFER.SVM enabled: %s\n",
         efer & MY_EFER_SVME ? "true" : "false");
}

uint32_t get_max_asids(void) {
  uint32_t cpuid_response;

  __asm__("cpuid" : "=r"(cpuid_response) : "a"(0x8000000A));

  return cpuid_response;
}

void vmsave(void *vmcb_addr) {
  printk(KERN_INFO "vmsave addr: %p\n", vmcb_addr);
  __asm__("vmsave" ::"a"(vmcb_addr));
}

void vmrun(void *vmcb_addr){
  __asm__("vmrun" ::"a"(vmcb_addr));
}

static struct vmcb *vmcb = NULL;
static void *hsave = NULL;
static struct page *hsave_page = NULL;
static struct page *vmcb_page = NULL;
bool start_vm(void) {
  uint32_t max_asids;

  // TODO: Check if memory is write back
  // Alloc page
  vmcb_page = alloc_page(GFP_KERNEL_ACCOUNT);
  if (vmcb_page == NULL) {
    printk(KERN_ERR "Could not allocate memory for vmcb\n");
    return false;
  }

  // Zero page
  vmcb = page_address(vmcb_page);
  clear_page(vmcb);
  printk(KERN_INFO "vmcb pointer: 0x%p\n", vmcb);

  // Check if vcmb is 4k aligned in memory
  if ((uint64_t)vmcb % 4096 != 0) {
    printk(KERN_ERR "VMCB is not 4k aligned!\n");
    return false;
  }

  // Alloc page
  hsave_page = alloc_page(GFP_KERNEL_ACCOUNT);
  if (hsave_page == NULL) {
    printk(KERN_ERR "Could not allocate memory for HSAVE\n");
    return false;
  }

  // Zero page
  hsave = page_address(hsave_page);
  clear_page(hsave);
  printk(KERN_INFO "hsave pointer is: 0x%p\n", hsave);


  // Check if hsave is 4k aligned in memory
  if ((uint64_t)hsave % 4096 != 0) {
    printk(KERN_ERR "HSAVE is not 4k aligned!\n");
    return false;
  }

  enableSVM_EFER();

  // Write buffer address to HSAVE msr
  writeMSR_U64(VM_HSAVE_PA_ADDR, *(uint64_t *)hsave);
  readMSR_U64(VM_HSAVE_PA_ADDR, (uint64_t *)hsave);
  printk(KERN_INFO "VM_HSAVE_PA_ADDR: 0x%p\n", hsave);

  // Read max asids
  max_asids = get_max_asids();
  printk(KERN_INFO "VM asid is: %d\n", max_asids);

  // Set asid in VMCB
  vmcb->control.asid = max_asids - 1;

  printk(KERN_INFO "Executing vmsave\n");
  vmsave((void*)vmcb);
  printk(KERN_INFO "Done executing vmsave\n");

  // Execute VMRUN instruction
  printk(KERN_INFO "Start executing vmrun\n");
  vmrun((void*)vmcb);
  printk(KERN_INFO "Done executing vmrun\n");

  return true;
}

static int my_init(void) {
  int ret = 0;
  enum SVM_SUPPORT svm;
  printk(KERN_INFO "==== LOADED HYPERVISOR DRIVER ====\n");

  if (!hasMsrSupport()) {
    printk(KERN_ERR "System does not have MSR support\n");
    return 1;
  }

  svm = checkSvmSupport();

  switch (svm) {
  case SVM_ALLOWED:
    printk(KERN_INFO "Has SVM support: true\n");
    break;
  case SVM_NOT_AVAIL:
    printk(KERN_ERR "Has SVM support: false\n");
    return 1;
  case SVM_DISABLED_WITH_KEY:
    printk(KERN_ERR "SVM is bios disabled with key\n");
    return 1;
  case SVM_DISABLED_AT_BIOS_NOT_UNLOCKABLE:
    printk(KERN_ERR "SVM is bios disabled not unlockable\n");
    return 1;
  }

  if (!start_vm()) {
    printk(KERN_ERR "vmrun failed\n");
    ret = 1;
    goto end;
  }

end:
  printk(KERN_INFO "Trying to free vmcb 0x%p hsave 0x%p\n", vmcb, hsave);
  if (vmcb_page != NULL) {
    __free_page(vmcb_page);
  }
  if (hsave_page != NULL) {
    __free_page(hsave_page);
  }
  return ret;
}

static void my_exit(void) {
  printk(KERN_INFO "Goodbye world.\n");

  return;
}

module_init(my_init);
module_exit(my_exit);
