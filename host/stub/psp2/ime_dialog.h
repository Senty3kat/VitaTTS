#pragma once
#include <psp2/types.h>
#include <psp2/common_dialog.h>
#define SCE_IME_DIALOG_MAX_TEXT_LENGTH 2048
#define SCE_IME_TYPE_DEFAULT 0
#define SCE_IME_DIALOG_BUTTON_ENTER 1
typedef struct { uint32_t supportedLanguages; int languagesForced; uint32_t type; uint32_t option; const uint16_t *title; uint32_t maxTextLength; const uint16_t *initialText; uint16_t *inputTextBuffer; } SceImeDialogParam;
typedef struct { int result; int button; } SceImeDialogResult;
void sceImeDialogParamInit(SceImeDialogParam *p); int sceImeDialogInit(const SceImeDialogParam *p);
int sceImeDialogGetStatus(void); int sceImeDialogGetResult(SceImeDialogResult *r); int sceImeDialogTerm(void);
