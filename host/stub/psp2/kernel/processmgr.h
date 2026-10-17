#pragma once
#include <psp2/types.h>
uint64_t sceKernelGetSystemTimeWide(void);
uint64_t sceKernelGetProcessTimeWide(void);
int sceKernelExitProcess(int);
int sceKernelPowerTick(int);
#define SCE_KERNEL_POWER_TICK_DEFAULT 0
