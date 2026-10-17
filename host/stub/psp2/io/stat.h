#pragma once
#include <psp2/types.h>
typedef struct { unsigned st_mode; } SceIoStat;
int sceIoMkdir(const char*,int);
