#pragma once
#include <psp2/types.h>
#define SCE_KERNEL_THREAD_ID_SELF 0
typedef int (*SceKernelThreadEntry)(SceSize,void*);
SceUID sceKernelCreateThread(const char*,SceKernelThreadEntry,int,SceSize,unsigned,int,void*);
int sceKernelStartThread(SceUID,SceSize,void*);
int sceKernelWaitThreadEnd(SceUID,int*,unsigned*);
int sceKernelDeleteThread(SceUID);
int sceKernelDelayThread(unsigned);
int sceKernelChangeThreadPriority(SceUID,int);
int sceKernelCreateMutex(const char*,unsigned,int,void*);
