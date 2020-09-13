#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

MODULE_AUTHOR("Qubasa Corp.");
MODULE_LICENSE("GPL v2");

// MASKS
const uint64_t LOW_64 = 0x00000000ffffffff;
const uint64_t HIGH_64 = ~LOW_64;

// MSR ADDRESSES
const unsigned int EFER_ADDR = 0xC0000080;
const unsigned int VM_CR_ADDR = 0xC0010114;
const unsigned int VM_HSAVE_PA_ADDR = 0xC0010117;

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

void readMSR(uint32_t id, uint32_t *hi, uint32_t *lo) {
  __asm__("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(id));
}

void writeMSR(uint32_t id, uint32_t hi, uint32_t lo) {
  __asm__("wrmsr" : : "a"(lo), "d"(hi), "c"(id));
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
  uint32_t efer;
  uint32_t high;
  uint64_t cr0;
  uint64_t cs;

  // Check CPL0
  // Processor must be in protected mode

  // Read VM_CR MSR
  readMSR(EFER_ADDR, &high, &efer);
  printk(KERN_INFO "Is EFER.SVM enabled: %s\n",
         efer & (1 << 12) ? "true" : "false");

  // Check if processor in protected mode
  __asm__("mov %0, cr0" : "=r"(cr0));
  printk(KERN_INFO "Is protected mode enabled: %s\n",
         cr0 & 1 ? "true" : "false");

  // Read the current CPL level
  __asm__("mov %0, cs" : "=r"(cs));
  printk(KERN_INFO "DPL is: %lld\n", cs & ((1 << 13) | (1 << 14)));

  // Enable EFER.SVM
  efer |= 1 << 12;
  writeMSR(EFER_ADDR, high, efer);
}

static void *vmcb = NULL;
static void *hsave = NULL;
bool vmrun(void) {
  uint32_t hsave_high;
  uint32_t hsave_low;

  // TODO: Check if memory is write back
  vmcb = (void *)kzalloc(4096, GFP_KERNEL);
  printk(KERN_INFO "vmcb pointer: 0x%lx\n", vmcb);

  if (vmcb == NULL) {
    printk(KERN_ERR "Could not allocate memory for vmcb\n");
    return false;
  }

  // Check if vcmb is 4k aligned in memory
  if ((uint64_t)vmcb % 4096 != 0) {
    printk(KERN_ERR "VMCB is not 4k aligned!\n");
    return false;
  }

  hsave = (void *)kzalloc(4096, GFP_KERNEL | GFP_HIGHUSER);
  printk(KERN_INFO "hsave pointer is: 0x%lx\n", hsave);
  if((uint64_t)hsave & (0xfff) > 0){
    printk(KERN_ERR "The low 12 bits are not zero!\n");
  }

  if (hsave == NULL) {
    printk(KERN_ERR "Could not allocate memory for HSAVE\n");
    return false;
  }

  // Check if vcmb is 4k aligned in memory
  if ((uint64_t)hsave % 4096 != 0) {
    printk(KERN_ERR "HSAVE is not 4k aligned!\n");
    return false;
  }

  enableSVM_EFER();

  hsave_high = (uint32_t)((uint64_t)hsave >> 32);
  hsave_low = (uint32_t)((uint64_t)hsave & LOW_64);

  // Write buffer address to HSAVE msr
  writeMSR(VM_HSAVE_PA_ADDR, hsave_high, hsave_low);
  readMSR(VM_HSAVE_PA_ADDR, &hsave_high, &hsave_low);

  hsave = (void *)((uint64_t)hsave_high << 32 | hsave_low);
  printk(KERN_INFO "VM_HSAVE_PA_ADDR: 0x%lx\n", hsave);

  // Execute VMRUN instruction
  printk(KERN_INFO "Start executing vmrun\n");
  __asm__("mov rax, %0" ::"r"(vmcb) : "rax");
  __asm__("vmrun");
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
    ret = 1;
    goto end;
  case SVM_DISABLED_WITH_KEY:
    printk(KERN_ERR "SVM is bios disabled with key\n");
    ret = 1;
    goto end;
  case SVM_DISABLED_AT_BIOS_NOT_UNLOCKABLE:
    printk(KERN_ERR "SVM is bios disabled not unlockable\n");
    ret = 1;
    goto end;
  }

  if (!vmrun()) {
    printk(KERN_ERR "vmrun failed\n");
    ret = 1;
    goto end;
  }

end:
  printk(KERN_INFO "Freeing and returning vmcb 0x%lx hsave 0x%lx\n", vmcb, hsave);
  kfree(vmcb);
  kfree(hsave);
  return ret;
}

static void my_exit(void) {
  printk(KERN_INFO "Goodbye world.\n");

  return;
}

module_init(my_init);
module_exit(my_exit);
