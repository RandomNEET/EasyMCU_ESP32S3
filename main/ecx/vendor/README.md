Vendored from EasyCon src/EasyCon.Vm/native/ecs_vm.{c,h}, commit
76cdb216d00fc5e0fb8510dede85390a6c5f3d40. License: GPL-3.0 (LICENSE).

Local changes: bound image table counts by image length, handle partial load
allocation failure during cleanup, and read instruction words without assuming
32-bit alignment on Xtensa; cast int32_t values in printf for the Xtensa ABI. Build limits call depth to 32 on the MCU.

Added ecs_vm_check_syscalls() for a load-time MCU syscall whitelist. FILE
feature headers can include PRINT while actual filesystem requests are rejected.
