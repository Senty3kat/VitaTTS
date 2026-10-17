#pragma once
#include <psp2/types.h>
typedef struct { uint8_t id; uint8_t unk; uint16_t x,y; uint8_t r[4]; } SceTouchReport;
typedef struct { uint64_t timeStamp; uint32_t status; uint32_t reportNum; SceTouchReport report[8]; } SceTouchData;
int sceTouchPeek(int,SceTouchData*,int); int sceTouchSetSamplingState(int,int); int sceTouchGetPanelInfo(int,void*);
#define SCE_TOUCH_PORT_FRONT 0
#define SCE_TOUCH_SAMPLING_STATE_START 1
#define SCE_TOUCH_PORT_BACK 1
#define SCE_TOUCH_PORT_BACK 1
int sceTouchRead(unsigned,SceTouchData*,unsigned);
