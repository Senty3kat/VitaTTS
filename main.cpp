#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

#include "debugScreen.h"
#include "soloud.h"
#include "soloud_speech.h"

static SoLoud::Soloud soloud;
static SoLoud::Speech speech;

static const char* phrases[] = {
    "Hello world.",
    "I am a machine. I can speak.",
    "This is a synthetic voice.",
    "The future is artificial.",
    "Repeat this sentence again and again."
};

static const int phraseCount =
    sizeof(phrases) / sizeof(phrases[0]);

static int selectedEngine = 0;
static int selectedPhrase = 0;

static void drawMenu()
{
    psvDebugScreenClear();

    psvDebugScreenPrintf("VitaTTS\n");
    psvDebugScreenPrintf("==============================\n\n");

    psvDebugScreenPrintf("TTS ENGINE\n");
    psvDebugScreenPrintf(
        "%s SoLoud Speech\n",
        selectedEngine == 0 ? ">" : " "
    );
    psvDebugScreenPrintf(
        "%s eSpeak Classic\n",
        selectedEngine == 1 ? ">" : " "
    );

    psvDebugScreenPrintf("\nTEXT PRESET\n");
    psvDebugScreenPrintf("%s\n\n", phrases[selectedPhrase]);

    psvDebugScreenPrintf("CONTROLS\n");
    psvDebugScreenPrintf("LEFT / RIGHT : Select engine\n");
    psvDebugScreenPrintf("UP / DOWN    : Select phrase\n");
    psvDebugScreenPrintf("CROSS        : Speak\n");
    psvDebugScreenPrintf("CIRCLE       : Stop\n");
    psvDebugScreenPrintf("START        : Exit\n\n");

    psvDebugScreenPrintf("Audio is generated in memory.\n");
    psvDebugScreenPrintf("No WAV files are created.\n");

    if (selectedEngine == 1) {
        psvDebugScreenPrintf(
            "\neSpeak Classic is not connected yet.\n"
        );
    }
}

int main()
{
    psvDebugScreenInit();

    if (soloud.init() != SoLoud::SO_NO_ERROR) {
        psvDebugScreenPrintf("SoLoud initialization failed.\n");
        sceKernelDelayThread(3000000);
        sceKernelExitProcess(1);
        return 1;
    }

    SceCtrlData pad;
    uint32_t previousButtons = 0;

    while (true) {
        sceCtrlPeekBufferPositive(&pad, 1);

        const uint32_t pressed =
            pad.buttons & ~previousButtons;

        previousButtons = pad.buttons;

        if (pressed & SCE_CTRL_LEFT) {
            selectedEngine = 0;
        }

        if (pressed & SCE_CTRL_RIGHT) {
            selectedEngine = 1;
        }

        if (pressed & SCE_CTRL_UP) {
            selectedPhrase =
                (selectedPhrase + phraseCount - 1)
                % phraseCount;
        }

        if (pressed & SCE_CTRL_DOWN) {
            selectedPhrase =
                (selectedPhrase + 1) % phraseCount;
        }

        if (pressed & SCE_CTRL_CIRCLE) {
            soloud.stopAll();
        }

        if (pressed & SCE_CTRL_CROSS) {
            if (selectedEngine == 0) {
                soloud.stopAll();
                speech.setText(phrases[selectedPhrase]);
                soloud.play(speech);
            }
        }

        if (pressed & SCE_CTRL_START) {
            break;
        }

        drawMenu();
        sceKernelDelayThread(16000);
    }

    soloud.deinit();
    sceKernelExitProcess(0);

    return 0;
}
