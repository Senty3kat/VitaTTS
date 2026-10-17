#pragma once
#include <psp2/types.h>
SceUID sceIoOpen(const char*,int,int); SceSSize sceIoRead(SceUID,void*,SceSize); SceSSize sceIoWrite(SceUID,const void*,SceSize); int sceIoClose(SceUID); int sceIoRemove(const char*); int sceIoRename(const char*,const char*); long long sceIoLseek(SceUID,long long,int);
int sceIoMkdir(const char*,int); int sceIoSync(const char*,int);
#define SCE_O_RDONLY 1
#define SCE_O_WRONLY 2
#define SCE_O_CREAT 0x200
#define SCE_O_TRUNC 0x400
#define SCE_SEEK_SET 0
#define SCE_SEEK_END 2
#define SCE_O_APPEND 0x100
