#pragma once

#include <Windows.h>
#include <cstdint>

enum class MirvPovVoiceMode { Team, All, Enemy };

void MirvPov_HookVoiceHud(HMODULE clientDll, bool location = true);
bool MirvPovVoice_IsEnabled();
void MirvPovVoice_SetEnabled(bool enabled);
MirvPovVoiceMode MirvPovVoice_GetMode();
const char * MirvPovVoice_GetModeName();
void MirvPovVoice_SetMode(MirvPovVoiceMode mode);
bool MirvPovVoice_AllowsPlayer(unsigned int playerSlot);
void MirvPov_ResetVoiceHud();
void MirvPovVoice_ResetDemoState();
void MirvPovVoice_OnLevelInit();
bool MirvPov_UpdateVoiceTeam();
void MirvPov_ClearSyntheticSpeaking();
bool MirvPovVoice_AfterRenderPass();

float MirvPovVoice_GameTime();
bool MirvPovVoice_TimeReady();
void MirvPovVoice_SetSelection(uint32_t low, uint32_t high);
bool MirvPovVoice_IsReady();
bool MirvPovVoice_LocationHooked();
bool MirvPovVoice_LocationReady();
bool MirvPovVoice_Cleanup();
