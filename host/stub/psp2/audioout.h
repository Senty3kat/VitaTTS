#pragma once
#include <psp2/types.h>
int sceAudioOutOpenPort(int,int,int,int); int sceAudioOutOutput(int,const void*); int sceAudioOutReleasePort(int); int sceAudioOutSetConfig(int,int,int,int);
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define SCE_AUDIO_OUT_PORT_TYPE_BGM 1
#define SCE_AUDIO_OUT_MODE_MONO 0
#define SCE_AUDIO_OUT_MODE_STEREO 1
