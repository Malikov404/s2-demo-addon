#pragma once
#include <Windows.h>
void MirvPovVoiceBan_Initialize(HMODULE clientDll);

void MirvPovVoiceBan_OnRenderPass();
void MirvPovVoiceBan_SetEnabled(bool enabled);
bool MirvPovVoiceBan_IsReady();
bool MirvPovVoiceBan_AvatarsReady();
void MirvPovVoiceBan_ResetDemoState();
