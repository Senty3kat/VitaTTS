#pragma once
#include <psp2/types.h>
#include <psp2/io/stat.h>
typedef struct SceIoDirent { SceIoStat d_stat; char d_name[256]; void *d_private; int dummy; } SceIoDirent;
SceUID sceIoDopen(const char *dirname);
int sceIoDread(SceUID fd, SceIoDirent *dir);
int sceIoDclose(SceUID fd);
