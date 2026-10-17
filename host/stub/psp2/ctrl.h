#pragma once
#include <psp2/types.h>
typedef struct { uint64_t timeStamp; unsigned buttons; unsigned char lx,ly,rx,ry; unsigned char reserved[20]; } SceCtrlData;
int sceCtrlPeekBufferPositive(int,SceCtrlData*,int); int sceCtrlSetSamplingMode(int);
#define SCE_CTRL_MODE_ANALOG 1
#define SCE_CTRL_SELECT 1
#define SCE_CTRL_START 8
#define SCE_CTRL_UP 0x10
#define SCE_CTRL_RIGHT 0x20
#define SCE_CTRL_DOWN 0x40
#define SCE_CTRL_LEFT 0x80
#define SCE_CTRL_LTRIGGER 0x100
#define SCE_CTRL_RTRIGGER 0x200
#define SCE_CTRL_TRIANGLE 0x1000
#define SCE_CTRL_CIRCLE 0x2000
#define SCE_CTRL_CROSS 0x4000
#define SCE_CTRL_SQUARE 0x8000
