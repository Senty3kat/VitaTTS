#ifndef DEBUG_SCREEN_H
#define DEBUG_SCREEN_H

#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <stdarg.h>

void psvDebugScreenInit();
void psvDebugScreenClear();
int  psvDebugScreenPrintf(const char *format, ...);
void psvDebugScreenSetXY(int x, int y);

#endif
