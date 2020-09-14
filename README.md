## Description
WORK IN PROGRESS  
A simple / minimalistic hypervisor for learning purposes, working with AMD-V.

## Get started
Compile with
```bash
$ make
$ insmod hypervisor.ko
```

Compile on NixOS with:
```bash
$ nix-shell shell.nix
$ make -C $(nix-build -E '(import <nixpkgs> {}).linux.dev' --no-out-link)/lib/modules/*/build M=$(pwd) modules
```

See logs with
```
$ dmesg --follow
```

## Resources
Look into `linux/arch/x86/kvm/svm.c`

Interesting functions are:
* pre_svm_run
* pre_sev_run
* svm_hardware_enable
* svm_create_vcpu
