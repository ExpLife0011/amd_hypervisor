#ifndef MY_HYPER_AMD
#define MY_HYPER_AMD
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>

struct __attribute__((__packed__)) vmcb_seg {
  u16 selector;
  u16 attrib;
  u32 limit;
  u64 base;
};

struct __attribute__((__packed__)) vmcb_control_area {
  u32 intercept_cr;
  u32 intercept_dr;
  u32 intercept_exceptions;
  u64 intercept;
  u8 reserved_1[40];
  u16 pause_filter_thresh;
  u16 pause_filter_count;
  u64 iopm_base_pa;
  u64 msrpm_base_pa;
  u64 tsc_offset;
  u32 asid;
  u8 tlb_ctl;
  u8 reserved_2[3];
  u32 int_ctl;
  u32 int_vector;
  u32 int_state;
  u8 reserved_3[4];
  u32 exit_code;
  u32 exit_code_hi;
  u64 exit_info_1;
  u64 exit_info_2;
  u32 exit_int_info;
  u32 exit_int_info_err;
  u64 nested_ctl;
  u64 avic_vapic_bar;
  u8 reserved_4[8];
  u32 event_inj;
  u32 event_inj_err;
  u64 nested_cr3;
  u64 virt_ext;
  u32 clean;
  u32 reserved_5;
  u64 next_rip;
  u8 insn_len;
  u8 insn_bytes[15];
  u64 avic_backing_page; /* Offset 0xe0 */
  u8 reserved_6[8];      /* Offset 0xe8 */
  u64 avic_logical_id;   /* Offset 0xf0 */
  u64 avic_physical_id;  /* Offset 0xf8 */
  u8 reserved_7[768];
};

struct __attribute__((__packed__)) vmcb_save_area {
  struct vmcb_seg es;
  struct vmcb_seg cs;
  struct vmcb_seg ss;
  struct vmcb_seg ds;
  struct vmcb_seg fs;
  struct vmcb_seg gs;
  struct vmcb_seg gdtr;
  struct vmcb_seg ldtr;
  struct vmcb_seg idtr;
  struct vmcb_seg tr;
  u8 reserved_1[43];
  u8 cpl;
  u8 reserved_2[4];
  u64 efer;
  u8 reserved_3[112];
  u64 cr4;
  u64 cr3;
  u64 cr0;
  u64 dr7;
  u64 dr6;
  u64 rflags;
  u64 rip;
  u8 reserved_4[88];
  u64 rsp;
  u8 reserved_5[24];
  u64 rax;
  u64 star;
  u64 lstar;
  u64 cstar;
  u64 sfmask;
  u64 kernel_gs_base;
  u64 sysenter_cs;
  u64 sysenter_esp;
  u64 sysenter_eip;
  u64 cr2;
  u8 reserved_6[32];
  u64 g_pat;
  u64 dbgctl;
  u64 br_from;
  u64 br_to;
  u64 last_excp_from;
  u64 last_excp_to;
};

struct __attribute__((__packed__)) vmcb {
  struct vmcb_control_area control;
  struct vmcb_save_area save;
};

// MASKS
static const uint64_t LOW_64 = 0x00000000ffffffff;
static const uint64_t HIGH_64 = ~LOW_64;
static const uint32_t MY_EFER_SVME = 1UL << 12;


// MSR ADDRESSES
static const unsigned int EFER_ADDR = 0xC0000080;
static const unsigned int VM_CR_ADDR = 0xC0010114;
static const unsigned int VM_HSAVE_PA_ADDR = 0xC0010117;

static void readMSR_U64(uint32_t id, uint64_t *complete) {
  uint64_t hi;
  uint64_t lo;
  __asm__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(id));

  *complete = hi << 32;
  *complete |= lo;
}

static void readMSR(uint32_t id, uint32_t *hi, uint32_t *lo) {
  __asm__("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(id));
}

static void writeMSR_U64(uint32_t id, uint64_t complete) {
  uint32_t hi;
  uint32_t lo;

  hi = (uint32_t)(complete >> 32);
  lo = (uint32_t)(complete & LOW_64);
  __asm__("wrmsr" : : "a"(lo), "d"(hi), "c"(id));
}

static void writeMSR(uint32_t id, uint32_t hi, uint32_t lo) {
  __asm__("wrmsr" : : "a"(lo), "d"(hi), "c"(id));
}
#endif
