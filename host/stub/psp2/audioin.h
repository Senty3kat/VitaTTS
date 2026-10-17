#pragma once
#include <psp2/types.h>
int sceAudioInOpenPort(int,int,int,int); int sceAudioInInput(int,void*); int sceAudioInReleasePort(int);
#define SCE_AUDIO_IN_PORT_TYPE_NORMAL 0
#define SCE_AUDIO_IN_PORT_TYPE_VOICE 1
#define SCE_AUDIO_IN_GETSTATUS_MUTE 0
#define SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO 0
